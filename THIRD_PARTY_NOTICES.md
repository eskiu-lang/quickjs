# Third-party notices

This package includes code from the projects below. Each is under the MIT license; the
full texts are in the files named here, and every prebuilt archive carries them.

| Component | Where | Copyright | License |
|---|---|---|---|
| [QuickJS](https://bellard.org/quickjs/) 2024-02-14 | `native/quickjs/` | 2017-2021 Fabrice Bellard, 2017-2021 Charlie Gordon | MIT, [native/quickjs/LICENSE](native/quickjs/LICENSE) |
| [rquickjs](https://github.com/DelSkayn/rquickjs) 0.6.2 patches to QuickJS | `native/patches/` (four of the five) | 2020 Mees Delzenne | MIT, [native/patches/LICENSE](native/patches/LICENSE) |

The sources in `native/quickjs/` are the copy rquickjs-sys 0.6.2 ships, with the five
patches of `native/patches/` already applied; the patch files are kept as the record of
each change. `drop_pending_jobs.patch` and everything outside these two directories is
this package's own code, under its [LICENSE](LICENSE).
