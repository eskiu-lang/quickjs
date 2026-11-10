# Platforms and the native library

The package's C side is one static library, `libeskqjs.a`: QuickJS from `native/quickjs`
and the layer `native/eskqjs.c` that `<quickjs/raw>` declares. `eskiu.toml` lists one
prebuilt archive of it per platform under `[native.eskqjs]`; `eskiuc` downloads the one it
builds for, checks its SHA-256 and links it.

## The engine

`native/quickjs` is Fabrice Bellard's QuickJS, release 2024-02-14 (the 2024-01-13
features: ES2023, top-level `await`, `Array.prototype.toSorted` and friends,
`Object.groupBy`, `Promise.withResolvers`, class static blocks), exactly as
[rquickjs-sys](https://crates.io/crates/rquickjs-sys) 0.6.2 ships it, with the four patches
rquickjs applies on every platform but MSVC, in `native/patches/` (already applied to the
sources; the files record each change, see [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)):

| Patch | From | What it does |
|---|---|---|
| `error_column_number.patch` | rquickjs | column numbers in stack traces (`file.js:3:9`) |
| `get_function_proto.patch` | rquickjs | `JS_GetFunctionProto` |
| `check_stack_overflow.patch` | rquickjs | a stack check that cannot wrap around |
| `infinity_handling.patch` | rquickjs | `INFINITY` in place of `1.0 / 0.0` |
| `drop_pending_jobs.patch` | this package | `JS_DropPendingJobs`, the first steps of `JS_FreeRuntime`, so leaks can be counted before freeing |

It is compiled with rquickjs's defines (`_GNU_SOURCE`, `CONFIG_BIGNUM`,
`CONFIG_VERSION`), with QuickJS's assertions on as there, at `-O2`.

### Why this QuickJS

The alternatives were the current Bellard release and [quickjs-ng](https://github.com/quickjs-ng/quickjs).
All three are MIT. quickjs-ng is the most actively maintained, builds with MSVC and has
moved past ES2023, and the current Bellard release has fixes since 2024. Both differ from
the engine Tallgrass's scripts run on today: quickjs-ng changes error messages, stack
format, some built-ins and parts of the C API, and either one would make a move from
rquickjs also an engine upgrade. Shipping the same source as rquickjs 0.6.2 lets 36,000
lines of game scripts move unchanged and be checked against the Rust host assertion for
assertion ([rquickjs.md](rquickjs.md)). An engine upgrade can follow as its own step, checked
against the same suites.

## Platforms

| Platform | Built with | Checked on this release |
|---|---|---|
| `macos-arm64` | Xcode clang, macOS 11+ | all tests, the Tallgrass suite |
| `macos-x86_64` | Xcode clang, macOS 10.15+ | all tests (Rosetta) |
| `linux-x86_64`, `linux-arm64` | `cc` on Linux, or `zig cc` (glibc 2.17+) | tests link; CI runs them |
| `windows-x86_64`, `windows-arm64` | llvm-mingw (UCRT), or MinGW-w64 gcc | tests link; CI runs x86-64 |
| `ios-arm64`, `ios-arm64-simulator`, `ios-x86_64-simulator` | Xcode clang, iOS 15+ | tests link; values, host and limits run on the arm64 simulator |
| `android-arm64`, `android-x86_64` | NDK clang, API 24+ | tests link |

The library links `libm` (Linux, Android) and pthreads (Linux, Windows), which
`<quickjs/raw>` asks for with `#pragma link`. On Windows the archive comes from a UCRT
toolchain, as `eskiuc`'s own llvm-mingw and MSYS2's UCRT64 are; an MSVCRT MinGW may not
link it. 32-bit targets are left out: `<quickjs/raw>` lays out `JSValue` as the 16-byte
struct of 64-bit builds.

## The web

On the web, a game already runs inside a JavaScript engine, so scripts should run there
directly instead of in QuickJS compiled to WebAssembly: they are faster under the
browser's JIT, need no second heap, and can be loaded with the page's own `import`.
Tallgrass's web player does this. `<quickjs/raw>` stops a `wasm32` build with an error
that says so. A program meant for both keeps its host functions behind one interface and
gives the web build an implementation over the browser (through emscripten's JavaScript
interop) in place of `<quickjs>`.

## Building the archives

`tools/build_native.sh` builds every platform the machine has a toolchain for and writes
`build/native/eskqjs-VERSION-PLATFORM.tar.gz` with `lib/libeskqjs.a`, the two headers and
the three licenses (this package, QuickJS, the rquickjs patches), plus
`build/native/SHA256SUMS`:

```bash
tools/build_native.sh                              # everything this machine can build
tools/build_native.sh macos-arm64 ios-arm64        # some platforms
ZIG=/path/to/zig tools/build_native.sh linux-x86_64 linux-arm64
```

A Mac with Xcode, the Android NDK and llvm-mingw (the one `eskiuc` installs under
`~/.eskiu/toolchains`) builds all 11; Linux archives need `zig` or a Linux host. The
archives are reproducible: object files carry no paths or time stamps, archive members and
tar entries have fixed dates and owners, and gzip writes no time, so the same toolchain
gives the same SHA-256.

To use a fresh build locally:

- `--install` unpacks each archive under its own SHA-256 in `~/.eskiu/native`; after
  `python3 -I tools/update_manifest.py` the manifest names it and `eskiuc` uses it.
- `--seed` unpacks it under the SHA-256 the manifest already names, so `eskiuc` links it
  in place of the published one without editing the manifest. CI does this.

## Releasing

1. Build every platform on one machine: `tools/build_native.sh` (with `ZIG` set).
2. `python3 -I tools/update_manifest.py` writes the sums and the release URLs
   (`https://github.com/eskiu-lang/quickjs/releases/download/vVERSION/...`) into
   `eskiu.toml`.
3. Commit, tag `vVERSION`, and upload `build/native/*.tar.gz` to that release
   (`gh release create vVERSION build/native/*.tar.gz`).
4. On a clean machine, `eskiuc test` downloads the published archive and must pass.

## The C layer

`native/eskqjs.h` is the contract between the two sides. QuickJS passes `JSValue` by value
and defines much of its API inline; `eskqjs.c` exports the same operations as plain
functions over `JSValue` pointers, so the Eskiu declarations are ordinary externs on every
target. It also holds what has to be C: the counting allocator (every block carries its
size, so `js_live_blocks()` is exact), the module loader and interrupt hooks that call back
into Eskiu, and the trampoline that turns a JavaScript call into a call of an Eskiu closure
by its index. `eq_abi_version()` guards against a library and a package from different
releases.
