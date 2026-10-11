---
name: zandronum-rebuild
description: Recompile the ForkUnderA engine (forkundera) after a C++ change and get the new binary running under fuactl. Use whenever engine source under src/zandronum/src changes -- features, the Diligent backend, the MCP bridge. On Windows do NOT use build.ps1 or windows_build*.ps1 for this; they throw on CMake dev warnings and do not pass the bridge flag. Use fuactl build.
---

# Rebuild the engine

fuactl needs a **bridge-enabled** build (`FUA_MCP_BRIDGE=ON`); the Vulkan/Diligent backend also needs
`FUA_DILIGENT=ON`, which is Windows-only today.

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
`DXSDK_DIR`, writes `build-win/build.log`, and fails loudly with the first errors. A running engine that
holds `build-win`'s binary is named as such instead of a bare LNK1104 -- quit your own instance by port.
The tree reconfigures by itself when `CMakeLists.txt` changes; the `(dev) find_package` warning is
harmless. Run it in the background -- an incremental build is a few minutes. Staging refuses while any
engine is running rather than killing them by name -- quit each by port first, or run the new build with
`FUACTL_ENGINE`.

## Adding a source file

- A feature `.cpp` goes in the `add_executable( zdoom ... )` list in `src/zandronum/src/CMakeLists.txt`,
  before `zzautozend.cpp` (see `features/README.md` for why it must not be a trailing `target_sources`).
- A `*_compute.cpp` the engine calls must be listed there too: the test build globs them, the engine
  build does not.
- A Diligent pass (`features/hwrender/diligent/*.cpp`) goes in the `FUA_DILIGENT` `target_sources` line
  AND both `set_source_files_properties(... c++17 ...)` lines -- Diligent's headers need C++17.

## Getting the new binary running

A build is not live until it is staged where fuactl looks (table above):

```bash
cp build-win/Release/forkundera.exe build-win/Release/forkundera.pdb dist-windows/
```

**A running engine holds that binary on Windows**, and the copy fails with "Device or resource busy" --
unchecked, the next run silently measures the OLD build. Quit every instance first, BY PORT
(`node src/cli.mjs ui exec quit --port P --token T`), never by process name, which would also kill the
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
then `build-tests/Release/zandrox_tests.exe --gtest_filter=...` for one suite).
