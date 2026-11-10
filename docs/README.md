# quickjs documentation

Guides and reference for quickjs. The [README](../README.md) has the overview.

<!-- pages -->
- [Moving from rquickjs](rquickjs.md)
- [Platforms and the native library](native.md)
- [API reference](api.md): every public function and type
<!-- /pages -->

## Runtimes and contexts

A `JsRuntime` is one QuickJS heap. It holds the limits, the module loader, the
interrupt handler and every host function defined in it. A `JsContext` is a global scope
inside a runtime, with its own `globalThis`; most programs need one. A runtime and its
contexts belong to one thread at a time.

```eskiu
*JsRuntime rt = js_runtime()?;   // a pointer: the runtime stays at one address
defer rt.release();
JsContext ctx = rt.context()?;
defer ctx.release();             // contexts go before their runtime
```

`ctx.eval(src, file)` runs a classic script and returns its completion value. ES modules
are covered under [Modules](#modules).

## Values

A `JsValue` is a JavaScript value plus the context it belongs to. Making one:

| JavaScript | Eskiu |
|---|---|
| `undefined`, `null` | `ctx.undefined()`, `ctx.null()` |
| number | `ctx.num(1.5)`, `ctx.int(42)` |
| boolean | `ctx.boolean(true)` |
| string | `ctx.str("text")`, `ctx.str_len(p, n)` (bytes may include NUL) |
| object, array | `ctx.object()`, `ctx.array()`, `ctx.num_array(&d[0], n)`, `ctx.str_array(&list)` |
| typed array | `ctx.typed_array(JS_F32, data, bytes)` (a copy), `ctx.array_buffer(data, bytes)` |
| JSON | `ctx.parse_json(text)?` |
| function | `ctx.function(name, length, closure)` (see [Host functions](#host-functions)) |

Reading one:

| Question | Call |
|---|---|
| what is it | `v.kind()` (`JS_NUMBER`, `JS_STRING`, `JS_ARRAY`, ...), `v.is_nullish()`, `v.is_function()` ... |
| as a number | `v.as_number()` (NaN if it does not convert), `v.as_int()` (ToInt32), `v.as_int64()` |
| as a boolean | `v.as_bool()` (truthiness) |
| as text | `v.as_string(&out)` appends UTF-8; `v.to_string()` returns a new `String` |
| as JSON | `v.to_json(indent, &out)` |
| bytes | `v.bytes(&len)` for a typed array, `DataView` or `ArrayBuffer`, else null |
| properties | `v.get("x")`, `v.get_num("x")`, `v.get_str("x", &out)`, `v.has("x")`, `v.keys(&list)` |
| elements | `v.length()`, `v.at(i)`, `v.numbers(&list)` |

Writing properties: `v.set("x", value)` (consumes `value`), `v.set_num`, `v.set_str`,
`v.set_bool`, `v.set_fn`, `v.set_at(i, value)`, `v.push(value)`, `v.delete("x")`. Globals:
`ctx.get_global(name)`, `ctx.set_global(name, value)`.

Calling: `ctx.call(&f, &this, args, n)`, `f.call(args, n)`, `obj.call_method("name", args,
n)`, `ctx.construct(&ctor, args, n)`. Arguments are an array of `JsValue` the caller
keeps; each call returns a `Result<JsValue, string>`.

The conversions that run script code (a `valueOf` or a getter) can throw. The accessors
above swallow such an exception and return a default (NaN, 0, `false`); `v.get` instead
returns a value of kind `JS_EXCEPTION`, which a host function can return as is to pass the
exception on.

## Host functions

A host function is an Eskiu closure of type `JsFn`:

```eskiu
type JsFn = fn(*JsContext, *JsArgs)->JsValue;
```

It captures what it needs by value; to change Eskiu state, capture a pointer. The closure
lives as long as the runtime.

```eskiu
*World w = &world;
JsValue engine = ctx.object();
engine.set_fn("addDecal", 5, JsValue(*JsContext c, *JsArgs a) {
    string path = a.str(0);                 // valid until the function returns
    double x = a.num(1);
    double ttl = a.opt_num(5, 3.0);         // 3.0 when missing, undefined or null
    w.add_decal(path, x, ttl);
    return c.undefined();
});
engine.set_fn("cell", 2, JsValue(*JsContext c, *JsArgs a) {
    JsValue o = c.object();
    o.set_num("height", w.height_at(a.int(0), a.int(1)));
    o.set_bool("walkable", w.walkable_at(a.int(0), a.int(1)));
    return o;                                // ownership goes to the script
});
ctx.set_global("engine", engine);
```

`JsArgs` reads arguments by position: `num`, `int`, `flag` (truthiness), `str`, their
`opt_` forms with a default, `has(i)`, `numbers(i, &list)` for an array argument,
`rest_nums(i, &list)` for `...rest`, `at(i)` for the value itself and `this()`.
Arguments are borrowed. To keep one past the call, such as a callback, take a reference
with `a.dup(i)` and free it when done:

```eskiu
engine.set_fn("onFrame", 1, JsValue(*JsContext c, *JsArgs a) {
    if (!a.at(0).is_function()) return c.throw_type_error("onFrame: a function is needed");
    w.listeners.push(a.dup(0));
    return c.undefined();
});
```

To fail, return `c.throw_error(msg)`, `c.throw_type_error(msg)`,
`c.throw_range_error(msg)` or `c.throw_value(v)`. The script sees an ordinary exception,
with its own stack, which `try`/`catch` can handle.

## Modules

A runtime loads the modules a script imports through its loader. Without one, `import`
fails. `rt.set_fs_loader(root)` reads files: a name starting with `.` is relative to the
importing module, any other name is relative to `root`, `.js` is added when the last
segment has no extension, and the path is normalized (`js_fs_resolve`). The same
resolved name always gives the same module instance.

For anything else (archives, a virtual file system, hot-reload caches), give two closures:

```eskiu
rt.set_loader(bool(string base, string name, *String out) {
    // base is the importer's resolved name; append the resolved name of `name`
    return my_resolve(base, name, out);
}, bool(string path, *String out) {
    // append the source of the resolved module
    return my_archive_read(path, out);
});
```

Evaluating:

- `ctx.eval_file(path)` reads a file and evaluates it as a module named by its
  normalized path.
- `ctx.eval_module(name, src)` evaluates source text as module `name`; its relative
  imports resolve against `name`.
- `ctx.import(name)` loads a module through the loader, or returns the instance already
  loaded.

Each returns the module namespace, whose exports are properties:
`ns.call_method("describe", null, 0)`. Jobs run until the module settles, so top-level
`await` works; `ctx.await(promise)` settles any other promise, `ctx.run_jobs()` runs the
queue once.

A module evaluated once stays loaded in its context. To reload changed scripts (an
editor's playtest), release the context and the runtime and build new ones; the tests
reload 20 times and check that nothing is left allocated.

## Errors

Every fallible call returns a `Result` whose error is the exception as text, the
message followed by the stack:

```
RangeError: deep
    at outer (caller.js:1:27)
    at <eval> (caller.js:2:1)
```

`js_last_error()` holds the same exception split into fields until the next error:
`name`, `message`, `stack`, `file`, `line` and `column` of the first frame that names a
file, and `interrupted` when a limit stopped the script. A thrown value that is not an
Error is reported by its text.

## Limits

| Limit | Call | What the script gets |
|---|---|---|
| heap | `rt.set_memory_limit(bytes)` | `InternalError: out of memory` |
| JavaScript stack | `rt.set_max_stack_size(bytes)` (default 256 KiB) | `InternalError: stack overflow` |
| time | `rt.set_time_limit(ms)`, counted from the call | `InternalError: interrupted` |
| anything | `rt.set_interrupt(closure)`, polled every few thousand instructions | `InternalError: interrupted` |

Interrupts cannot be caught by the script. After any of them the context keeps working.
The stack limit must stay below the real stack of the thread running the scripts; a
debug build uses more stack per JavaScript call, which is why Tallgrass's QA runner uses
8 MiB. Call `rt.update_stack_top()` after moving a runtime to another thread.
`rt.gc()` collects cycles now, `rt.set_gc_threshold(bytes)` tunes when it runs on its
own, and `rt.memory()` reports heap size and object counts.

## Ownership

Every `JsValue` a function returns belongs to the caller and is freed with `free()`;
`defer v.free();` is the usual form. The exceptions are the arguments of a host function
and the values passed to calls, which are borrowed, and the values given to `set`,
`set_at`, `push`, `set_global` and `throw_value`, which those calls consume. Numbers,
booleans, null and undefined need no freeing, but freeing them is harmless.

The runtime allocates through a counting allocator. `rt.release()` collects garbage and
returns the number of objects still alive. A non-zero count means some value was never
freed; the heap is then left allocated rather than freed with live objects, which
QuickJS forbids. `js_live_blocks()` and `js_live_bytes()` count what every runtime of
the process still holds, so a test can end with:

```eskiu
check("no objects leaked", rt.release() == 0);
check("every block freed", js_live_blocks() == 0);
```

## Examples

[`examples/run.esk`](../examples/run.esk) runs a module headless: it gives it an `engine`
with `log` and `now`, calls `onBoot` once and `onUpdate` for a number of frames, and prints
any error with its file and line.

```
$ eskiuc build
$ build/quickjs tests/data/game/main.js 60
boot route1 ready=yes
ran 60 frames; heap 97 KiB, 188 objects
```

## Contributing

`eskiuc test` runs the tests in `tests/`, each a program that exits 0 when it passes:
values, host functions, the module graph in `tests/data/game`, limits and leaks. They use
the native library of `eskiu.toml`; to test changes to `native/`, rebuild it for the host
first (`tools/build_native.sh --seed macos-arm64`, see [native.md](native.md)).
CI does the same on every platform it tests, so it never depends on published archives.
`tools/battle_rules.esk` is an optional local check against Tallgrass
([rquickjs.md](rquickjs.md#checked-against-tallgrass)); it is not part of the tests.
`docs/api.md` is generated from the comments in `src/` by `python3 -I tools/gen_api.py`.
