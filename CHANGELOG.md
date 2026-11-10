# Changelog

All notable changes to this package are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/).

## [0.1.0] - 2026-11-10

### Added

- `<quickjs>`: runtimes and contexts (`js_runtime`, `JsRuntime_context`, `release`),
  classic scripts (`JsContext_eval`) and ES modules (`eval_module`, `eval_file`,
  `import`) with top-level `await` and dynamic `import()` settled through the job queue
  (`await`, `run_jobs`).
- Module loading through closures (`JsRuntime_set_loader`) or from files
  (`set_fs_loader`, `js_fs_resolve`, `js_path_normalize`): relative imports from the
  importer, bare imports from a root, `.js` added when there is no extension.
- Values in both directions: numbers, strings with embedded NULs, booleans, null,
  undefined, arrays, plain objects, JSON (`parse_json`, `to_json`), typed arrays and
  ArrayBuffers (`typed_array`, `array_buffer`, `bytes`); kinds, properties, keys,
  elements, calls, methods and constructors.
- Host functions from Eskiu closures (`JsContext_function`, `JsValue_set_fn`) with
  `JsArgs` accessors for numbers, integers, flags, strings, optional and rest arguments,
  arrays and kept callbacks (`dup`); errors thrown from Eskiu (`throw_error`,
  `throw_type_error`, `throw_range_error`, `throw_value`).
- Errors as `Result` text with the message and stack, and `js_last_error()` with name,
  message, stack, file, line, column and whether a limit interrupted the script.
- Limits: memory (`set_memory_limit`), JavaScript stack (`set_max_stack_size`,
  `update_stack_top`), time (`set_time_limit`) and an interrupt closure
  (`set_interrupt`, `clear_interrupt`); `gc`, `set_gc_threshold`, `memory`.
- Leak checks: a counting allocator under every runtime (`js_live_blocks`,
  `js_live_bytes`) and `JsRuntime_release` reporting objects never freed instead of
  failing QuickJS's assertion.
- `<quickjs/raw>`: the C layer `native/eskqjs.h` over QuickJS 2024-02-14, the release
  rquickjs 0.6.2 bundles, with its four patches and `drop_pending_jobs.patch`.
- Prebuilt reproducible archives for macOS (arm64, x86-64), Linux (x86-64, arm64),
  Windows (x86-64, arm64), iOS (device, arm64 and x86-64 simulator) and Android (arm64,
  x86-64), built by `tools/build_native.sh` and listed in `eskiu.toml` by
  `tools/update_manifest.py`.
- Tests for values, host functions, a module graph with relative imports, a cycle,
  top-level await and reloads, limits and leaks; `examples/run.esk`, a headless runner;
  `tools/battle_rules.esk`, an optional local check that runs Tallgrass's
  graphics-free battle rule suite under this package (`TALLGRASS_DIR`, not run in CI).
- `THIRD_PARTY_NOTICES.md` and the licenses of QuickJS and the rquickjs patches in every
  archive.
