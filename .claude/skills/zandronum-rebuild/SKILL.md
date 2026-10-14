---
name: zandronum-rebuild
description: Recompile the ForkUnderA engine (forkundera) after a C++ change and get the new binary running under fuactl. Use whenever engine source under src/zandronum/src changes -- features, the Diligent backend, the MCP bridge. On Windows do NOT use windows_build*.ps1 for this; they throw on CMake dev warnings and do not pass the bridge flag. Use fuactl build.
---

# Rebuild the engine

fuactl needs a **bridge-enabled** build (`FUA_MCP_BRIDGE=ON`). The Vulkan/Diligent backend
(`FUA_DILIGENT`) is ON by default for every client build on Windows, Linux and macOS, and OFF for a
dedicated server; `-DFUA_DILIGENT=OFF` builds GL (Legacy) alone.

| platform | build | staged binary fuactl runs |
|---|---|---|
| Windows | `fuactl build` on the existing `build-win/` tree (below) | `dist-windows/forkundera.exe` |
| macOS | `ZX_MCP_BRIDGE=1 ./mac_compile.sh` | `build/ForkUnderA.app/Contents/MacOS/forkundera` |
| Linux | `cmake -S src/zandronum -B build-linux -DFUA_MCP_BRIDGE=ON` once (it stays in the cache), then `./linux_compile.sh --no-package` | `FUACTL_ENGINE=<repo>/build-linux/forkundera`, or copy it to `dist-linux/forkundera` |

## Windows: fuactl build

```bash
node tools/fuactl/src/cli.mjs build --no-stage --tests   # mid-investigation: engine and unit tests, run, nothing staged
node tools/fuactl/src/cli.mjs build                      # when the fix is done: compiled and staged
```

It finds Visual Studio's CMake itself (Git Bash has none on PATH; `FUACTL_CMAKE` overrides), sets
`DXSDK_DIR`, writes `build-win/build.log`, and fails loudly with the first errors. It also repacks the
data pk3s from `wadsrc*/` (the `pk3` targets) and stages them with the exe; before 2026-10-13 it built
`zdoom` alone, so a `wadsrc/static` change (a menu, a lump) never reached the binary beside it. A running engine that
holds `build-win`'s binary is named as such instead of a bare LNK1104 -- quit your own instance by port.
The tree reconfigures by itself when `CMakeLists.txt` changes; the `(dev) find_package` warning is
harmless. Run it in the background -- an incremental build is a few minutes. Staging refuses while any
engine is running rather than killing them by name -- quit each by port first, or run the new build with
`FUACTL_ENGINE`.

## Adding a source file

- A feature `.cpp` goes in the `add_executable( zdoom ... )` list in `src/zandronum/src/CMakeLists.txt`,
  before `zzautozend.cpp` (see `features/README.md` for why it must not be a trailing `target_sources`).
- A `*_compute.cpp` needs no list entry: the engine (`ZX_COMPUTE_SOURCES`) and the test build both glob them.
- A source for one platform only goes in that platform's list (`PLAT_SDL_SOURCES`, ...). On Windows the
  other platforms' lists are added as header-only, which wins over an entry in the main list too, so a
  Linux-only file is never compiled here; to compile-check one, move it to the main list for one build.
- A Diligent pass (`features/hwrender/diligent/*.cpp`) goes in the `FUA_DILIGENT_SOURCES` list, which also
  gives it the C++17 that Diligent's headers need. A platform's window code (`dgwin32.cpp`, `dgsdl.cpp`,
  `dgcocoa.mm`) goes in that platform's branch of `FUA_DILIGENT_PLATFORM_SOURCES` instead, and includes no
  Diligent header (`dgplatform.h` is the seam).

## Getting the new binary running

A build is not live until it is staged where fuactl looks (table above). `node src/cli.mjs build` stages
the exe, its `.pdb`, the data `*.pk3`s and `catalogue/`; after a `--no-stage` build, quit the engines and
run `build` again rather than copying by hand, since a hand copy has missed the pk3s, the catalogue and the
symbols before.

**A running engine holds that binary on Windows**, and staging fails -- `build` refuses while any engine
runs, so the next run never silently measures the OLD build. Quit every instance first, BY PORT
(`node src/cli.mjs quit --port P [--token T]`), never by process name, which would also kill the
user's own instance and other sessions' harnesses. Then confirm none are left: `tasklist /FI
"IMAGENAME eq forkundera.exe"` on Windows, `pgrep -l forkundera` elsewhere.

If the user's instance must stay up, run the new build without staging it:
`FUACTL_ENGINE=<repo>/build-win/Release/forkundera.exe`. That folder has no runtime DLLs of its own --
copy `dist-windows/*.dll` beside it once, or the launch dies with 0xc0000135 (fuactl names it).
Captures then land in `build-win/Release/sweep/`.

## Keep the old binary for a before/after

Copy the staged binary somewhere before staging over it. It is the before half of every comparison,
and the only way to check whether an odd behaviour predates the change. To RUN a copy, its folder
needs the runtime DLLs AND the `*.pk3` files beside it -- without `fua_core_*.pk3` the engine stops on
a modal error box on the user's screen. fuactl refuses such a folder before launching it.

## Unit tests

`tests/` builds separately; see the `writing-tests` skill (on Windows: `build --no-stage --tests` above,
then `build-tests/Release/zandrox_tests.exe --gtest_filter=...` for one suite, and `fuactl coverage --since
main` for the 100% gate on what changed).
