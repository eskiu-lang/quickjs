<p align="center">
  <img src="https://raw.githubusercontent.com/eskiu-lang/eskiu/main/assets/logo.png" alt="Eskiu, a kitten with a ball of yarn" width="140">
</p>

<h2 align="center">quickjs</h2>
<p align="center">JavaScript for Eskiu programs, over QuickJS.</p>
<p align="center"><sub>Part of the <a href="https://github.com/eskiu-lang">Eskiu</a> project</sub></p>
<p align="center">
  <a href="https://eskiu-lang.org">eskiu-lang.org</a> &nbsp;&middot;&nbsp;
  <a href="docs/README.md">Guide</a> &nbsp;&middot;&nbsp;
  <a href="docs/api.md">API</a> &nbsp;&middot;&nbsp;
  <a href="https://github.com/eskiu-lang/eskiu">The language</a>
</p>

<p align="center">
  <a href="https://github.com/eskiu-lang/quickjs/actions/workflows/ci.yml"><img src="https://github.com/eskiu-lang/quickjs/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <img src="https://img.shields.io/badge/version-0.1.0-6448d4" alt="Version 0.1.0">
  <img src="https://img.shields.io/badge/QuickJS-2024--02--14-orange" alt="QuickJS 2024-02-14">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue" alt="License: MIT"></a>
</p>

---

`quickjs` embeds Fabrice Bellard's [QuickJS](https://bellard.org/quickjs/) in an Eskiu
program, for games and tools that keep their logic in JavaScript. It is the engine
[rquickjs](https://github.com/DelSkayn/rquickjs) 0.6.2 bundles, with the same patches,
so scripts written against a Rust host behave the same under an Eskiu one.

- **ES modules.** Files or in-memory sources, relative and bare imports through a
  resolver and a loader you write as closures, or the ready-made file loader.
  Top-level `await` and dynamic `import()` settle before a call returns.
- **Values both ways.** Numbers, strings (UTF-8, embedded NULs), booleans, null and
  undefined, arrays, plain objects, JSON and typed arrays, with typed accessors.
- **Host functions.** Eskiu closures exposed as JavaScript functions, with their state
  captured; optional and rest arguments; callbacks kept and called later.
- **Errors with their place.** A script error comes back as a `Result` with the message
  and the stack, and its file, line and column split out.
- **Limits.** Memory cap, JavaScript stack limit, a time limit and an interrupt handler
  for runaway scripts; the context stays usable afterwards.
- **Leak checks.** Every runtime allocates through a counting allocator; `release()`
  reports objects never freed, and `js_live_blocks()` must be 0 once all is released.
- **Prebuilt for 11 platforms.** macOS, Linux and Windows (x86-64, arm64), iOS (device
  and simulator) and Android, fetched by `eskiuc` like any native library.

## A game loop

```eskiu
import <io>;
import <quickjs>;

struct Player { double x; }

Result<int, string> boot(*Player p) {
    *JsRuntime rt = js_runtime()?;
    defer rt.release();
    rt.set_fs_loader("scripts");          // "./a.js" from the importer, "lib/b.js" from scripts/
    JsContext ctx = rt.context()?;
    defer ctx.release();

    JsValue engine = ctx.object();
    engine.set_fn("setX", 1, JsValue(*JsContext c, *JsArgs a) {
        p.x = a.num(0);
        return c.undefined();
    });
    engine.set_fn("log", 1, JsValue(*JsContext c, *JsArgs a) {
        printf("%s\n", a.str(0));
        return c.undefined();
    });
    ctx.set_global("engine", engine);

    JsValue ns = ctx.eval_file("scripts/main.js")?;
    defer ns.free();
    JsValue booted = ctx.call_global_opt("onBoot", null, 0)?;
    booted.free();
    JsValue dt = ctx.num(1.0 / 60.0);
    for (frame in 0..600) {
        JsValue r = ctx.call_global("onUpdate", &dt, 1)?;
        r.free();
    }
    return Ok<int, string>(0);
}
```

A script error stops the loop with its text:

```
TypeError: cannot read property 'rows' of null
    at explode (scripts/battle/table.js:3:16)
    at onUpdate (scripts/main.js:21:5)
```

## Documentation

- [Runtimes, contexts and values](docs/README.md#runtimes-and-contexts)
- [Host functions](docs/README.md#host-functions)
- [Modules and loaders](docs/README.md#modules)
- [Errors](docs/README.md#errors)
- [Limits and interrupts](docs/README.md#limits)
- [Ownership and leak checks](docs/README.md#ownership)
- [Moving from rquickjs](docs/rquickjs.md)
- [Platforms and the native library](docs/native.md)
- [API reference](docs/api.md): every public function and type

## Install

In an Eskiu package (Eskiu 0.15 or newer):

```bash
eskiuc add quickjs --git https://github.com/eskiu-lang/quickjs --rev v0.1.0
eskiuc build
```

The first build downloads the prebuilt QuickJS library for your platform and checks its
SHA-256. On the web, scripts run in the browser's own JavaScript engine instead; see
[platforms](docs/native.md#the-web).

## License

MIT. See [LICENSE](LICENSE). QuickJS and the rquickjs patches are MIT too; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The prebuilt archives carry all three
licenses.
