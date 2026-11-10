/* eskqjs: see eskqjs.h. */
#include "eskqjs.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

/* ---- Callbacks ---------------------------------------------------------- */

static eq_call_fn g_call;
static eq_resolve_fn g_resolve;
static eq_load_fn g_load;
static eq_interrupt_fn g_interrupt;

int eq_abi_version(void) { return EQ_ABI_VERSION; }

const char *eq_quickjs_version(void) { return CONFIG_VERSION; }

void eq_set_callbacks(eq_call_fn call, eq_resolve_fn resolve, eq_load_fn load,
                      eq_interrupt_fn interrupt)
{
    g_call = call;
    g_resolve = resolve;
    g_load = load;
    g_interrupt = interrupt;
}

/* ---- Counting allocator ------------------------------------------------- */

/* Every block carries a 16-byte header with its size, so the allocator knows
 * the usable size on every platform and the live totals stay exact. */
#define EQ_HEAD 16

static volatile int64_t g_live_blocks;
static volatile int64_t g_live_bytes;

static void eq_count(int64_t blocks, int64_t bytes)
{
    __atomic_add_fetch(&g_live_blocks, blocks, __ATOMIC_RELAXED);
    __atomic_add_fetch(&g_live_bytes, bytes, __ATOMIC_RELAXED);
}

int64_t eq_live_blocks(void) { return __atomic_load_n(&g_live_blocks, __ATOMIC_RELAXED); }
int64_t eq_live_bytes(void) { return __atomic_load_n(&g_live_bytes, __ATOMIC_RELAXED); }

static size_t eq_block_size(const void *ptr)
{
    if (!ptr)
        return 0;
    return *(const size_t *)((const char *)ptr - EQ_HEAD);
}

static void *eq_malloc(JSMallocState *s, size_t size)
{
    char *p;
    if (size == 0)
        size = 1;
    if (s->malloc_size + size > s->malloc_limit)
        return NULL;
    p = malloc(size + EQ_HEAD);
    if (!p)
        return NULL;
    *(size_t *)p = size;
    s->malloc_count++;
    s->malloc_size += size + EQ_HEAD;
    eq_count(1, (int64_t)size);
    return p + EQ_HEAD;
}

static void eq_free_block(JSMallocState *s, void *ptr)
{
    size_t size;
    if (!ptr)
        return;
    size = eq_block_size(ptr);
    s->malloc_count--;
    s->malloc_size -= size + EQ_HEAD;
    eq_count(-1, -(int64_t)size);
    free((char *)ptr - EQ_HEAD);
}

static void *eq_realloc(JSMallocState *s, void *ptr, size_t size)
{
    size_t old;
    char *p;
    if (!ptr) {
        if (size == 0)
            return NULL;
        return eq_malloc(s, size);
    }
    old = eq_block_size(ptr);
    if (size == 0) {
        eq_free_block(s, ptr);
        return NULL;
    }
    if (s->malloc_size + size - old > s->malloc_limit)
        return NULL;
    p = realloc((char *)ptr - EQ_HEAD, size + EQ_HEAD);
    if (!p)
        return NULL;
    *(size_t *)p = size;
    s->malloc_size += size - old;
    eq_count(0, (int64_t)size - (int64_t)old);
    return p + EQ_HEAD;
}

static size_t eq_usable_size(const void *ptr) { return eq_block_size(ptr); }

static const JSMallocFunctions eq_malloc_funcs = {
    eq_malloc, eq_free_block, eq_realloc, eq_usable_size,
};

/* ---- Runtime ------------------------------------------------------------ */

typedef struct EqRuntime {
    void *host;
    int64_t deadline_ms; /* 0: no time limit */
    int use_interrupt;
    int interrupted;
} EqRuntime;

static int64_t eq_now_ms(void)
{
#if defined(_WIN32)
    return (int64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

static EqRuntime *eq_rt(JSRuntime *rt) { return (EqRuntime *)JS_GetRuntimeOpaque(rt); }

static int eq_interrupt_handler(JSRuntime *rt, void *opaque)
{
    EqRuntime *r = (EqRuntime *)opaque;
    (void)rt;
    if (r->deadline_ms && eq_now_ms() > r->deadline_ms) {
        r->interrupted = 1;
        return 1;
    }
    if (r->use_interrupt && g_interrupt && g_interrupt(r->host)) {
        r->interrupted = 1;
        return 1;
    }
    return 0;
}

static void eq_update_interrupt(JSRuntime *rt)
{
    EqRuntime *r = eq_rt(rt);
    if (r->deadline_ms || r->use_interrupt)
        JS_SetInterruptHandler(rt, eq_interrupt_handler, r);
    else
        JS_SetInterruptHandler(rt, NULL, NULL);
}

JSRuntime *eq_runtime_new(void *host)
{
    JSRuntime *rt;
    EqRuntime *r = calloc(1, sizeof(EqRuntime));
    if (!r)
        return NULL;
    rt = JS_NewRuntime2(&eq_malloc_funcs, NULL);
    if (!rt) {
        free(r);
        return NULL;
    }
    r->host = host;
    JS_SetRuntimeOpaque(rt, r);
    return rt;
}

int64_t eq_runtime_free(JSRuntime *rt)
{
    JSMemoryUsage mu;
    EqRuntime *r;
    if (!rt)
        return 0;
    r = eq_rt(rt);
    /* what JS_FreeRuntime frees before its leak assertion: jobs never run (a
       pending import(), an unhandled then()) are not leaks */
    JS_DropPendingJobs(rt);
    JS_RunGC(rt);
    JS_ComputeMemoryUsage(rt, &mu);
    if (mu.obj_count > 0)
        return mu.obj_count;
    JS_FreeRuntime(rt);
    free(r);
    return 0;
}

void eq_set_memory_limit(JSRuntime *rt, int64_t bytes)
{
    JS_SetMemoryLimit(rt, bytes <= 0 ? (size_t)-1 : (size_t)bytes);
}

void eq_set_max_stack_size(JSRuntime *rt, int64_t bytes)
{
    JS_SetMaxStackSize(rt, bytes < 0 ? 0 : (size_t)bytes);
}

void eq_update_stack_top(JSRuntime *rt) { JS_UpdateStackTop(rt); }

void eq_set_gc_threshold(JSRuntime *rt, int64_t bytes) { JS_SetGCThreshold(rt, (size_t)bytes); }

void eq_run_gc(JSRuntime *rt) { JS_RunGC(rt); }

void eq_set_time_limit(JSRuntime *rt, int64_t ms)
{
    eq_rt(rt)->deadline_ms = ms > 0 ? eq_now_ms() + ms : 0;
    eq_update_interrupt(rt);
}

void eq_use_interrupt(JSRuntime *rt, int on)
{
    eq_rt(rt)->use_interrupt = on != 0;
    eq_update_interrupt(rt);
}

int eq_was_interrupted(JSRuntime *rt)
{
    EqRuntime *r = eq_rt(rt);
    int was = r->interrupted;
    r->interrupted = 0;
    return was;
}

void eq_memory_usage(JSRuntime *rt, int64_t *out)
{
    JSMemoryUsage mu;
    JS_ComputeMemoryUsage(rt, &mu);
    out[0] = mu.malloc_size;
    out[1] = mu.malloc_limit;
    out[2] = mu.malloc_count;
    out[3] = mu.obj_count;
    out[4] = mu.str_count;
    out[5] = mu.atom_count;
    out[6] = mu.js_func_count;
    out[7] = mu.memory_used_size;
}

int eq_run_jobs(JSRuntime *rt, JSContext **err_ctx)
{
    int n = 0;
    *err_ctx = NULL;
    for (;;) {
        JSContext *c = NULL;
        int r = JS_ExecutePendingJob(rt, &c);
        if (r == 0)
            return n;
        if (r < 0) {
            *err_ctx = c;
            return -1;
        }
        n++;
    }
}

int eq_jobs_pending(JSRuntime *rt) { return JS_IsJobPending(rt); }

/* ---- Module loader ------------------------------------------------------ */

static char *eq_module_normalize(JSContext *ctx, const char *base, const char *name, void *opaque)
{
    EqRuntime *r = (EqRuntime *)opaque;
    char *got, *out;
    got = g_resolve ? g_resolve(r->host, base, name) : NULL;
    if (!got) {
        JS_ThrowReferenceError(ctx, "could not resolve module '%s' imported from '%s'", name, base);
        return NULL;
    }
    out = js_strdup(ctx, got);
    free(got);
    return out;
}

static JSModuleDef *eq_module_load(JSContext *ctx, const char *path, void *opaque)
{
    EqRuntime *r = (EqRuntime *)opaque;
    int64_t len = 0;
    char *src, *copy;
    JSValue fn;
    src = g_load ? g_load(r->host, path, &len) : NULL;
    if (!src) {
        JS_ThrowReferenceError(ctx, "could not load module '%s'", path);
        return NULL;
    }
    copy = js_malloc(ctx, (size_t)len + 1);
    if (!copy) {
        free(src);
        return NULL;
    }
    memcpy(copy, src, (size_t)len);
    copy[len] = 0;
    free(src);
    fn = JS_Eval(ctx, copy, (size_t)len, path, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    js_free(ctx, copy);
    if (JS_IsException(fn))
        return NULL;
    /* the module stays referenced by the context's module list */
    JS_FreeValue(ctx, fn);
    return (JSModuleDef *)JS_VALUE_GET_PTR(fn);
}

void eq_use_module_loader(JSRuntime *rt, int on)
{
    if (on)
        JS_SetModuleLoaderFunc(rt, eq_module_normalize, eq_module_load, eq_rt(rt));
    else
        JS_SetModuleLoaderFunc(rt, NULL, NULL, NULL);
}

/* ---- Context ------------------------------------------------------------ */

JSContext *eq_context_new(JSRuntime *rt) { return JS_NewContext(rt); }

void eq_context_free(JSContext *ctx)
{
    if (ctx)
        JS_FreeContext(ctx);
}

JSRuntime *eq_context_runtime(JSContext *ctx) { return JS_GetRuntime(ctx); }

void *eq_context_host(JSContext *ctx) { return eq_rt(JS_GetRuntime(ctx))->host; }

/* ---- Values ------------------------------------------------------------- */

void eq_free(JSContext *ctx, JSValue *v)
{
    JS_FreeValue(ctx, *v);
    *v = JS_UNDEFINED;
}

void eq_dup(JSContext *ctx, const JSValue *v, JSValue *out) { *out = JS_DupValue(ctx, *v); }

int eq_tag(const JSValue *v) { return JS_VALUE_GET_TAG(*v); }

int eq_is_exception(const JSValue *v) { return JS_IsException(*v); }

int eq_kind(JSContext *ctx, const JSValue *v)
{
    JSValue x = *v;
    switch (JS_VALUE_GET_NORM_TAG(x)) {
    case JS_TAG_UNDEFINED:
    case JS_TAG_UNINITIALIZED:
        return EQ_UNDEFINED;
    case JS_TAG_NULL:
        return EQ_NULL;
    case JS_TAG_BOOL:
        return EQ_BOOL;
    case JS_TAG_INT:
    case JS_TAG_FLOAT64:
        return EQ_NUMBER;
    case JS_TAG_STRING:
        return EQ_STRING;
    case JS_TAG_SYMBOL:
        return EQ_SYMBOL;
    case JS_TAG_BIG_INT:
        return EQ_BIGINT;
    case JS_TAG_EXCEPTION:
        return EQ_EXCEPTION;
    case JS_TAG_OBJECT:
        if (JS_IsFunction(ctx, x))
            return EQ_FUNCTION;
        if (JS_IsArray(ctx, x) > 0)
            return EQ_ARRAY;
        if (JS_IsError(ctx, x))
            return EQ_ERROR;
        return EQ_OBJECT;
    default:
        return EQ_OTHER;
    }
}

void eq_undefined(JSValue *out) { *out = JS_UNDEFINED; }
void eq_null(JSValue *out) { *out = JS_NULL; }
void eq_exception(JSValue *out) { *out = JS_EXCEPTION; }
void eq_new_bool(JSContext *ctx, int b, JSValue *out) { *out = JS_NewBool(ctx, b); }
void eq_new_int32(JSContext *ctx, int32_t n, JSValue *out) { *out = JS_NewInt32(ctx, n); }
void eq_new_int64(JSContext *ctx, int64_t n, JSValue *out) { *out = JS_NewInt64(ctx, n); }
void eq_new_float64(JSContext *ctx, double d, JSValue *out) { *out = JS_NewFloat64(ctx, d); }

static int eq_result(JSValue v, JSValue *out)
{
    *out = v;
    return JS_IsException(v) ? -1 : 0;
}

int eq_new_string(JSContext *ctx, const char *s, int64_t len, JSValue *out)
{
    if (!s)
        s = "";
    if (len < 0)
        len = (int64_t)strlen(s);
    return eq_result(JS_NewStringLen(ctx, s, (size_t)len), out);
}

int eq_new_object(JSContext *ctx, JSValue *out) { return eq_result(JS_NewObject(ctx), out); }
int eq_new_array(JSContext *ctx, JSValue *out) { return eq_result(JS_NewArray(ctx), out); }

static JSValue eq_trampoline(JSContext *ctx, JSValueConst this_val, int argc,
                             JSValueConst *argv, int magic, JSValue *data)
{
    JSValue ret = JS_UNDEFINED;
    int32_t id = 0;
    EqRuntime *r = eq_rt(JS_GetRuntime(ctx));
    (void)magic;
    JS_ToInt32(ctx, &id, data[0]);
    if (!g_call)
        return JS_ThrowInternalError(ctx, "no host function dispatcher");
    g_call(r->host, ctx, id, &this_val, argc, argv, &ret);
    return ret;
}

int eq_new_function(JSContext *ctx, const char *name, int length, int id, JSValue *out)
{
    JSValue data = JS_NewInt32(ctx, id);
    JSValue fn = JS_NewCFunctionData(ctx, eq_trampoline, length, 0, 1, &data);
    if (JS_IsException(fn))
        return eq_result(fn, out);
    if (name && *name) {
        /* Function.prototype.name, as a C function defined with a name has */
        JS_DefinePropertyValueStr(ctx, fn, "name", JS_NewString(ctx, name), JS_PROP_CONFIGURABLE);
    }
    return eq_result(fn, out);
}

static const char *const eq_typed_names[] = {
    "Uint8Array", "Int8Array", "Uint16Array", "Int16Array",
    "Uint32Array", "Int32Array", "Float32Array", "Float64Array",
};

int eq_new_array_buffer(JSContext *ctx, const void *data, int64_t bytes, JSValue *out)
{
    static const uint8_t none = 0;
    return eq_result(JS_NewArrayBufferCopy(ctx, data ? data : &none, (size_t)bytes), out);
}

int eq_new_typed_array(JSContext *ctx, int kind, const void *data, int64_t bytes, JSValue *out)
{
    JSValue buf, global, ctor, arr;
    if (kind < 0 || kind > EQ_F64) {
        *out = JS_ThrowRangeError(ctx, "unknown typed array kind %d", kind);
        return -1;
    }
    if (eq_new_array_buffer(ctx, data, bytes, &buf) < 0)
        return -1;
    global = JS_GetGlobalObject(ctx);
    ctor = JS_GetPropertyStr(ctx, global, eq_typed_names[kind]);
    JS_FreeValue(ctx, global);
    arr = JS_CallConstructor(ctx, ctor, 1, (JSValueConst *)&buf);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, buf);
    return eq_result(arr, out);
}

/* ---- Conversions -------------------------------------------------------- */

int eq_to_float64(JSContext *ctx, const JSValue *v, double *out) { return JS_ToFloat64(ctx, out, *v); }
int eq_to_int32(JSContext *ctx, const JSValue *v, int32_t *out) { return JS_ToInt32(ctx, out, *v); }
int eq_to_int64(JSContext *ctx, const JSValue *v, int64_t *out) { return JS_ToInt64(ctx, out, *v); }
int eq_to_bool(JSContext *ctx, const JSValue *v) { return JS_ToBool(ctx, *v); }

const char *eq_to_cstring(JSContext *ctx, const JSValue *v, int64_t *len)
{
    size_t n = 0;
    const char *s = JS_ToCStringLen(ctx, &n, *v);
    if (len)
        *len = s ? (int64_t)n : 0;
    return s;
}

void eq_free_cstring(JSContext *ctx, const char *s)
{
    if (s)
        JS_FreeCString(ctx, s);
}

uint8_t *eq_buffer_data(JSContext *ctx, const JSValue *v, int64_t *bytes)
{
    size_t offset = 0, len = 0, bpe = 0, size = 0;
    uint8_t *p;
    JSValue buf;
    *bytes = 0;
    if (!JS_IsObject(*v))
        return NULL;
    buf = JS_GetTypedArrayBuffer(ctx, *v, &offset, &len, &bpe);
    if (JS_IsException(buf)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        p = JS_GetArrayBuffer(ctx, &size, *v);
        if (!p) {
            JS_FreeValue(ctx, JS_GetException(ctx));
            return NULL;
        }
        *bytes = (int64_t)size;
        return p;
    }
    p = JS_GetArrayBuffer(ctx, &size, buf);
    JS_FreeValue(ctx, buf);
    if (!p) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return NULL;
    }
    *bytes = (int64_t)len;
    return p + offset;
}

/* ---- Properties --------------------------------------------------------- */

int eq_get(JSContext *ctx, const JSValue *obj, const char *name, JSValue *out)
{
    return eq_result(JS_GetPropertyStr(ctx, *obj, name), out);
}

int eq_set(JSContext *ctx, const JSValue *obj, const char *name, JSValue *val)
{
    JSValue v = *val;
    *val = JS_UNDEFINED;
    return JS_SetPropertyStr(ctx, *obj, name, v) < 0 ? -1 : 0;
}

int eq_get_index(JSContext *ctx, const JSValue *obj, int64_t i, JSValue *out)
{
    JSValue key = JS_NewInt64(ctx, i);
    JSAtom a = JS_ValueToAtom(ctx, key);
    JS_FreeValue(ctx, key);
    if (a == JS_ATOM_NULL)
        return eq_result(JS_EXCEPTION, out);
    *out = JS_GetProperty(ctx, *obj, a);
    JS_FreeAtom(ctx, a);
    return JS_IsException(*out) ? -1 : 0;
}

int eq_set_index(JSContext *ctx, const JSValue *obj, int64_t i, JSValue *val)
{
    JSValue v = *val;
    *val = JS_UNDEFINED;
    return JS_SetPropertyInt64(ctx, *obj, i, v) < 0 ? -1 : 0;
}

int eq_has(JSContext *ctx, const JSValue *obj, const char *name)
{
    JSAtom a;
    int r;
    if (!JS_IsObject(*obj))
        return 0;
    a = JS_NewAtom(ctx, name);
    if (a == JS_ATOM_NULL)
        return -1;
    r = JS_HasProperty(ctx, *obj, a);
    JS_FreeAtom(ctx, a);
    return r;
}

int eq_delete(JSContext *ctx, const JSValue *obj, const char *name)
{
    JSAtom a = JS_NewAtom(ctx, name);
    int r;
    if (a == JS_ATOM_NULL)
        return -1;
    r = JS_DeleteProperty(ctx, *obj, a, JS_PROP_THROW);
    JS_FreeAtom(ctx, a);
    return r;
}

int eq_length(JSContext *ctx, const JSValue *obj, int64_t *out)
{
    JSValue len = JS_GetPropertyStr(ctx, *obj, "length");
    int r;
    *out = 0;
    if (JS_IsException(len))
        return -1;
    r = JS_ToInt64(ctx, out, len);
    JS_FreeValue(ctx, len);
    return r;
}

int eq_keys(JSContext *ctx, const JSValue *obj, JSValue *out)
{
    JSPropertyEnum *tab = NULL;
    uint32_t n = 0, i;
    JSValue arr;
    if (JS_GetOwnPropertyNames(ctx, &tab, &n, *obj, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
        return eq_result(JS_EXCEPTION, out);
    arr = JS_NewArray(ctx);
    for (i = 0; i < n; i++) {
        if (!JS_IsException(arr) &&
            JS_SetPropertyUint32(ctx, arr, i, JS_AtomToString(ctx, tab[i].atom)) < 0) {
            JS_FreeValue(ctx, arr);
            arr = JS_EXCEPTION;
        }
        JS_FreeAtom(ctx, tab[i].atom);
    }
    js_free(ctx, tab);
    return eq_result(arr, out);
}

void eq_global(JSContext *ctx, JSValue *out) { *out = JS_GetGlobalObject(ctx); }

int eq_define_const(JSContext *ctx, const JSValue *obj, const char *name, JSValue *val)
{
    JSValue v = *val;
    *val = JS_UNDEFINED;
    return JS_DefinePropertyValueStr(ctx, *obj, name, v, JS_PROP_THROW) < 0 ? -1 : 0;
}

/* ---- Evaluation --------------------------------------------------------- */

/* JS_Eval reads one byte past the source, which must be a NUL. */
static JSValue eq_eval_copy(JSContext *ctx, const char *src, int64_t len, const char *file, int flags)
{
    char *copy;
    JSValue r;
    if (len < 0)
        len = (int64_t)strlen(src);
    copy = js_malloc(ctx, (size_t)len + 1);
    if (!copy)
        return JS_EXCEPTION;
    memcpy(copy, src, (size_t)len);
    copy[len] = 0;
    r = JS_Eval(ctx, copy, (size_t)len, file, flags);
    js_free(ctx, copy);
    return r;
}

int eq_eval(JSContext *ctx, const char *src, int64_t len, const char *file, int flags, JSValue *out)
{
    int f = JS_EVAL_TYPE_GLOBAL | ((flags & 1) ? JS_EVAL_FLAG_STRICT : 0);
    return eq_result(eq_eval_copy(ctx, src, len, file ? file : "<eval>", f), out);
}

int eq_await(JSContext *ctx, JSValue *promise, JSValue *out)
{
    JSValue p = *promise;
    JSRuntime *rt = JS_GetRuntime(ctx);
    *promise = JS_UNDEFINED;
    *out = JS_UNDEFINED;
    if (JS_IsException(p))
        return eq_result(p, out);
    for (;;) {
        int state = JS_PromiseState(ctx, p);
        if (state < 0) { /* not a promise */
            *out = p;
            return 0;
        }
        if (state == JS_PROMISE_FULFILLED) {
            *out = JS_PromiseResult(ctx, p);
            JS_FreeValue(ctx, p);
            return 0;
        }
        if (state == JS_PROMISE_REJECTED) {
            JSValue reason = JS_PromiseResult(ctx, p);
            JS_FreeValue(ctx, p);
            *out = JS_Throw(ctx, reason);
            return -1;
        }
        {
            JSContext *c = NULL;
            int r = JS_ExecutePendingJob(rt, &c);
            if (r < 0) {
                /* a job threw: report it in place of the promise */
                JS_FreeValue(ctx, p);
                *out = JS_EXCEPTION;
                if (c && c != ctx)
                    JS_Throw(ctx, JS_GetException(c));
                return -1;
            }
            if (r == 0) {
                JS_FreeValue(ctx, p);
                *out = JS_ThrowInternalError(ctx, "promise did not settle (no pending job left)");
                return -1;
            }
        }
    }
}

int eq_eval_module(JSContext *ctx, const char *src, int64_t len, const char *name, JSValue *ns)
{
    JSValue fn, promise, done;
    JSModuleDef *m;
    *ns = JS_UNDEFINED;
    fn = eq_eval_copy(ctx, src, len, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(fn))
        return eq_result(fn, ns);
    m = (JSModuleDef *)JS_VALUE_GET_PTR(fn);
    promise = JS_EvalFunction(ctx, fn);
    if (eq_await(ctx, &promise, &done) < 0)
        return eq_result(JS_EXCEPTION, ns);
    JS_FreeValue(ctx, done);
    return eq_result(JS_GetModuleNamespace(ctx, m), ns);
}

int eq_import(JSContext *ctx, const char *name, JSValue *ns)
{
    JSValue promise = JS_LoadModule(ctx, "", name);
    return eq_await(ctx, &promise, ns);
}

int eq_call(JSContext *ctx, const JSValue *fn, const JSValue *this_val, int argc,
            const JSValue *argv, JSValue *out)
{
    JSValue t = this_val ? *this_val : JS_UNDEFINED;
    return eq_result(JS_Call(ctx, *fn, t, argc, (JSValueConst *)argv), out);
}

int eq_construct(JSContext *ctx, const JSValue *fn, int argc, const JSValue *argv, JSValue *out)
{
    return eq_result(JS_CallConstructor(ctx, *fn, argc, (JSValueConst *)argv), out);
}

int eq_parse_json(JSContext *ctx, const char *text, int64_t len, const char *file, JSValue *out)
{
    char *copy;
    JSValue r;
    if (len < 0)
        len = (int64_t)strlen(text);
    copy = js_malloc(ctx, (size_t)len + 1);
    if (!copy)
        return eq_result(JS_EXCEPTION, out);
    memcpy(copy, text, (size_t)len);
    copy[len] = 0;
    r = JS_ParseJSON(ctx, copy, (size_t)len, file ? file : "<json>");
    js_free(ctx, copy);
    return eq_result(r, out);
}

int eq_stringify(JSContext *ctx, const JSValue *v, int indent, JSValue *out)
{
    JSValue space = indent > 0 ? JS_NewInt32(ctx, indent) : JS_UNDEFINED;
    JSValue r = JS_JSONStringify(ctx, *v, JS_UNDEFINED, space);
    return eq_result(r, out);
}

/* ---- Exceptions --------------------------------------------------------- */

void eq_throw(JSContext *ctx, int kind, const char *msg, JSValue *out)
{
    if (!msg)
        msg = "";
    switch (kind) {
    case EQ_THROW_TYPE: *out = JS_ThrowTypeError(ctx, "%s", msg); break;
    case EQ_THROW_RANGE: *out = JS_ThrowRangeError(ctx, "%s", msg); break;
    case EQ_THROW_REFERENCE: *out = JS_ThrowReferenceError(ctx, "%s", msg); break;
    case EQ_THROW_SYNTAX: *out = JS_ThrowSyntaxError(ctx, "%s", msg); break;
    case EQ_THROW_INTERNAL: *out = JS_ThrowInternalError(ctx, "%s", msg); break;
    default: {
        /* new Error(msg), so the error records the script's stack */
        JSValue global = JS_GetGlobalObject(ctx);
        JSValue ctor = JS_GetPropertyStr(ctx, global, "Error");
        JSValue text = JS_NewString(ctx, msg);
        JSValue e = JS_CallConstructor(ctx, ctor, 1, (JSValueConst *)&text);
        JS_FreeValue(ctx, text);
        JS_FreeValue(ctx, ctor);
        JS_FreeValue(ctx, global);
        *out = JS_IsException(e) ? e : JS_Throw(ctx, e);
        break;
    }
    }
}

void eq_throw_value(JSContext *ctx, JSValue *val, JSValue *out)
{
    JSValue v = *val;
    *val = JS_UNDEFINED;
    *out = JS_Throw(ctx, v);
}

void eq_take_exception(JSContext *ctx, JSValue *out) { *out = JS_GetException(ctx); }

int eq_has_exception(JSContext *ctx)
{
    JSValue e = JS_GetException(ctx);
    if (JS_IsNull(e))
        return 0;
    JS_Throw(ctx, e);
    return 1;
}
