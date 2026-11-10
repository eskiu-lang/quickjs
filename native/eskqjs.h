/* eskqjs: the C layer between QuickJS and the Eskiu package <quickjs>.
 *
 * QuickJS keeps much of its API in static inline functions and passes JSValue
 * by value. This layer exports plain functions that take JSValue by pointer, so
 * the Eskiu side declares them as ordinary externs on every target. It also
 * owns the parts that must be C: the counting allocator, the module loader and
 * interrupt hooks, and the trampoline for host functions.
 *
 * Ownership: a JSValue written to an `out` parameter is owned by the caller and
 * must be freed with eq_free. A `const JSValue *` parameter is borrowed. A
 * parameter documented as "consumed" is freed by the call, even on failure.
 */
#ifndef ESKQJS_H
#define ESKQJS_H

#include <stddef.h>
#include <stdint.h>
#include "quickjs/quickjs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EQ_ABI_VERSION 1

/* Value kinds reported by eq_kind. */
enum {
    EQ_UNDEFINED = 0,
    EQ_NULL = 1,
    EQ_BOOL = 2,
    EQ_NUMBER = 3,
    EQ_STRING = 4,
    EQ_SYMBOL = 5,
    EQ_BIGINT = 6,
    EQ_OBJECT = 7,
    EQ_ARRAY = 8,
    EQ_FUNCTION = 9,
    EQ_ERROR = 10,
    EQ_EXCEPTION = 11,
    EQ_OTHER = 12
};

/* Error constructors for eq_throw. */
enum {
    EQ_THROW_ERROR = 0,
    EQ_THROW_TYPE = 1,
    EQ_THROW_RANGE = 2,
    EQ_THROW_REFERENCE = 3,
    EQ_THROW_SYNTAX = 4,
    EQ_THROW_INTERNAL = 5
};

/* Typed array kinds for eq_new_typed_array. */
enum {
    EQ_U8 = 0,
    EQ_I8 = 1,
    EQ_U16 = 2,
    EQ_I16 = 3,
    EQ_U32 = 4,
    EQ_I32 = 5,
    EQ_F32 = 6,
    EQ_F64 = 7
};

/* Callbacks into the embedder, set once with eq_set_callbacks. `host` is the
 * pointer given to eq_runtime_new.
 *   call:      runs host function `id`; writes its result to *ret (JS_EXCEPTION
 *              after eq_throw) and returns 0.
 *   resolve:   returns the module name for `name` imported from `base` in a
 *              malloc'd string, or NULL when it does not resolve.
 *   load:      returns the source of module `path` in a malloc'd buffer of
 *              *len bytes, or NULL when it cannot be read.
 *   interrupt: returns nonzero to stop the running script. */
typedef int (*eq_call_fn)(void *host, JSContext *ctx, int id, const JSValue *this_val,
                          int argc, const JSValue *argv, JSValue *ret);
typedef char *(*eq_resolve_fn)(void *host, const char *base, const char *name);
typedef char *(*eq_load_fn)(void *host, const char *path, int64_t *len);
typedef int (*eq_interrupt_fn)(void *host);

int eq_abi_version(void);
const char *eq_quickjs_version(void);
void eq_set_callbacks(eq_call_fn call, eq_resolve_fn resolve, eq_load_fn load,
                      eq_interrupt_fn interrupt);

/* Live blocks and bytes allocated by every runtime of the process. Both are 0
 * once each runtime has been freed without leaks. */
int64_t eq_live_blocks(void);
int64_t eq_live_bytes(void);

/* Runtime. */
JSRuntime *eq_runtime_new(void *host);
/* Frees the runtime after a garbage collection. Returns 0, or the number of
 * objects still alive (values never freed): the runtime is then left allocated
 * instead of failing QuickJS's assertion. */
int64_t eq_runtime_free(JSRuntime *rt);
void eq_set_memory_limit(JSRuntime *rt, int64_t bytes);
void eq_set_max_stack_size(JSRuntime *rt, int64_t bytes);
void eq_update_stack_top(JSRuntime *rt);
void eq_set_gc_threshold(JSRuntime *rt, int64_t bytes);
void eq_run_gc(JSRuntime *rt);
/* Interrupts scripts that run longer than `ms` from now (0: no limit). */
void eq_set_time_limit(JSRuntime *rt, int64_t ms);
/* Asks the embedder's interrupt callback (0: do not). */
void eq_use_interrupt(JSRuntime *rt, int on);
/* 1 when the last exception came from an interrupt (time limit or callback). */
int eq_was_interrupted(JSRuntime *rt);
void eq_use_module_loader(JSRuntime *rt, int on);
/* Memory figures: malloc_size, malloc_limit, malloc_count, obj_count,
 * str_count, atom_count, js_func_count, memory_used_size, in that order. */
void eq_memory_usage(JSRuntime *rt, int64_t *out8);
/* Runs pending jobs (promise reactions). Returns how many ran, or -1 when one
 * threw: *err_ctx is then the context holding the exception. */
int eq_run_jobs(JSRuntime *rt, JSContext **err_ctx);
int eq_jobs_pending(JSRuntime *rt);

/* Context. */
JSContext *eq_context_new(JSRuntime *rt);
void eq_context_free(JSContext *ctx);
JSRuntime *eq_context_runtime(JSContext *ctx);
/* The `host` pointer given to eq_runtime_new for the context's runtime. */
void *eq_context_host(JSContext *ctx);

/* Values. */
void eq_free(JSContext *ctx, JSValue *v);
void eq_dup(JSContext *ctx, const JSValue *v, JSValue *out);
int eq_tag(const JSValue *v);
int eq_kind(JSContext *ctx, const JSValue *v);
int eq_is_exception(const JSValue *v);
void eq_undefined(JSValue *out);
void eq_null(JSValue *out);
void eq_exception(JSValue *out);
void eq_new_bool(JSContext *ctx, int b, JSValue *out);
void eq_new_int32(JSContext *ctx, int32_t n, JSValue *out);
void eq_new_int64(JSContext *ctx, int64_t n, JSValue *out);
void eq_new_float64(JSContext *ctx, double d, JSValue *out);
int eq_new_string(JSContext *ctx, const char *s, int64_t len, JSValue *out);
int eq_new_object(JSContext *ctx, JSValue *out);
int eq_new_array(JSContext *ctx, JSValue *out);
int eq_new_function(JSContext *ctx, const char *name, int length, int id, JSValue *out);
/* A copy of `bytes` bytes as a typed array of `kind` (EQ_U8 ...). */
int eq_new_typed_array(JSContext *ctx, int kind, const void *data, int64_t bytes, JSValue *out);
int eq_new_array_buffer(JSContext *ctx, const void *data, int64_t bytes, JSValue *out);

/* Conversions. Each returns 0, or -1 with an exception pending. */
int eq_to_float64(JSContext *ctx, const JSValue *v, double *out);
int eq_to_int32(JSContext *ctx, const JSValue *v, int32_t *out);
int eq_to_int64(JSContext *ctx, const JSValue *v, int64_t *out);
int eq_to_bool(JSContext *ctx, const JSValue *v);
/* UTF-8 text of `v` (ToString), freed with eq_free_cstring; NULL on exception. */
const char *eq_to_cstring(JSContext *ctx, const JSValue *v, int64_t *len);
void eq_free_cstring(JSContext *ctx, const char *s);
/* The bytes of a typed array, DataView or ArrayBuffer: NULL when `v` is none
 * of those (no exception left pending). */
uint8_t *eq_buffer_data(JSContext *ctx, const JSValue *v, int64_t *bytes);

/* Properties. The set functions consume `val`. */
int eq_get(JSContext *ctx, const JSValue *obj, const char *name, JSValue *out);
int eq_set(JSContext *ctx, const JSValue *obj, const char *name, JSValue *val);
int eq_get_index(JSContext *ctx, const JSValue *obj, int64_t i, JSValue *out);
int eq_set_index(JSContext *ctx, const JSValue *obj, int64_t i, JSValue *val);
/* 1, 0, or -1 with an exception pending. */
int eq_has(JSContext *ctx, const JSValue *obj, const char *name);
int eq_delete(JSContext *ctx, const JSValue *obj, const char *name);
int eq_length(JSContext *ctx, const JSValue *obj, int64_t *out);
/* The own enumerable string keys of `obj`, as an array of strings. */
int eq_keys(JSContext *ctx, const JSValue *obj, JSValue *out);
void eq_global(JSContext *ctx, JSValue *out);
/* Defines property `name` as read-only, non-enumerable (consumes `val`). */
int eq_define_const(JSContext *ctx, const JSValue *obj, const char *name, JSValue *val);

/* Evaluation. `flags` is 0 for a global script, 1 for a strict script.
 * Each returns 0, or -1 with an exception pending. */
int eq_eval(JSContext *ctx, const char *src, int64_t len, const char *file, int flags, JSValue *out);
/* Evaluates `src` as the ES module `name` (resolving its imports with the
 * loader), runs pending jobs until it settles and writes its namespace. */
int eq_eval_module(JSContext *ctx, const char *src, int64_t len, const char *name, JSValue *ns);
/* Imports module `name` (already resolved) through the loader and writes its namespace. */
int eq_import(JSContext *ctx, const char *name, JSValue *ns);
int eq_call(JSContext *ctx, const JSValue *fn, const JSValue *this_val, int argc,
            const JSValue *argv, JSValue *out);
int eq_construct(JSContext *ctx, const JSValue *fn, int argc, const JSValue *argv, JSValue *out);
int eq_parse_json(JSContext *ctx, const char *text, int64_t len, const char *file, JSValue *out);
/* JSON.stringify(v, null, indent); writes undefined for a value JSON leaves out. */
int eq_stringify(JSContext *ctx, const JSValue *v, int indent, JSValue *out);
/* Settles a promise by running jobs; writes its value or throws its reason.
 * Consumes `promise`. A value that is not a promise is written back as is. */
int eq_await(JSContext *ctx, JSValue *promise, JSValue *out);

/* Exceptions. */
/* Throws a new error of `kind` with `msg`; writes JS_EXCEPTION. */
void eq_throw(JSContext *ctx, int kind, const char *msg, JSValue *out);
/* Throws `val` (consumed); writes JS_EXCEPTION. */
void eq_throw_value(JSContext *ctx, JSValue *val, JSValue *out);
/* Takes the pending exception (undefined... when none). */
void eq_take_exception(JSContext *ctx, JSValue *out);
int eq_has_exception(JSContext *ctx);

#ifdef __cplusplus
}
#endif

#endif
