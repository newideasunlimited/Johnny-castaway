# Vendored engine: Johnny Reborn (HD fork)

This directory is a trimmed copy of
[bigfnj/johnny-castaway-rebornHD](https://github.com/bigfnj/johnny-castaway-rebornHD),
an open-source C engine for Johnny Castaway. It descends from
[jno6809/jc_reborn](https://github.com/jno6809/jc_reborn) by Jérémie Guillaume,
which built on Hans Milling's resource-format work. License: GPL-3.0-or-later
(see `LICENSE`).

| | |
|---|---|
| Upstream commit | `a0c2d032e75ac24145acce5c505cf3ac396b9427` (2026-09-17) |
| Upstream version | 1.0.1 |
| Copied | `src/engine`, `src/data`, `platform/{platform.h, jc_resources.h, png_*, zipvfs.*, platform_macos.m}`, `third_party/miniz`, `LICENSE` |
| Not copied | Windows, Linux and Web backends, tests, docs, artwork sources, CMake files |

## Local changes

The engine was written as a standalone program. To run it inside a macOS
screen saver bundle, where it lives on a worker thread of `legacyScreenSaver`,
these changes were made:

1. **`jcExit()` instead of `exit()`** (`utils.h`, `utils.c`, `events.c`,
   `jc_reborn.c`). Every process-exit path now goes through `jcExit(code)`.
   Without `JC_EMBEDDED` it is `exit(code)`. With `JC_EMBEDDED` it calls
   `platformEmbeddedExit(code)`, which the saver backend implements with
   `pthread_exit()`, so quitting the engine never kills the host process.
2. **`fatalError()` reports to the host** (`utils.c`). With `JC_EMBEDDED`,
   the formatted message is also passed to `platformEmbeddedFatal()` so the
   screen saver can display it (there is no console to read `stderr` from).
3. **`JC_MAIN`** (`jc_reborn.c`). `main` is defined as `JC_MAIN`, which
   defaults to `main`. The saver builds with `-DJC_MAIN=jcEngineMain` and
   calls the entry point from its engine thread with a synthetic `argv`.
4. **`JC_REBORN_DATA`** (`platform/zipvfs.c`). If set, this environment
   variable is tried first when opening `scrantic_data.zip`, because the
   bundle's data file is neither in the working directory nor next to the
   host executable. `ZIPVFS_MAX_TRIED` was raised from 4 to 5 for it.
5. **`atexit()` registered once** (`events.c`). The host can start the
   engine several times in one process.
6. **`platform_soft.{h,c}`** (new). The software-surface code that every
   upstream backend duplicates, shared by the two new backends:
   `saver/platform_saver.c` (macOS screen saver) and
   `tools/platform_headless.c` (no display; used for tests and frame dumps).

`platform_macos.m` is kept unmodified for the `make cli` windowed test build.
