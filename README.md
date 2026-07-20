# ZandroX

Build harness for the [Zandronum](https://zandronum.com) source port, targeting **macOS, Linux and Windows** with OpenAL audio on all three.

Based on [rc4l/zandronum-macos-compile](https://github.com/rc4l/zandronum-macos-compile).

## Build

### macOS (native arm64)
Requires Xcode command line tools (`xcode-select --install`) and [Homebrew](https://brew.sh).
```bash
./build.sh            # native arm64, OpenAL audio (default)
SOUND=0 ./build.sh    # no in-game audio
```
Output: **`build/ZandroX.app`** (`open build/ZandroX.app`).

### Linux (via Docker, reproducible)
Requires Docker. Runs on macOS too — you get a binary for the host architecture.
```bash
./package-linux.sh                 # full client
SERVERONLY=ON ./package-linux.sh   # headless server
```
Output: **`dist-linux/ZandroX-linux-<arch>.tar.gz`**. Needs `libopenal1`,
`libsndfile1` and `libmpg123-0` on the target machine. The script fails the build
if the binary did not link OpenAL, rather than shipping a silent client.

### Windows (x64)
Built in CI only (MSVC + vcpkg for the OpenAL/decoder DLLs) — see the
`build-windows` job in `.github/workflows/manual-build-latest.yml`. The legacy
VS2008 `.vcproj`/`.sln` files in the tree are unused by the CMake build.

## Releases

The **Build ZandroX** workflow builds all three platforms and can publish them as a
GitHub Release. Either:

- push a tag — `git tag v0.1.0 && git push origin v0.1.0`; or
- run the workflow manually from the Actions tab, tick **release**, and give a version.

Releases are gated on the test suite and on a per-platform check that OpenAL is
actually linked. Ordinary pushes to `main` still build all three but publish nothing.

The macOS app is **not signed or notarized**, so Gatekeeper blocks it on first launch:
```bash
xattr -dr com.apple.quarantine ZandroX.app
```

## License

ZandroX is distributed under the **GNU General Public License v3.0** (see `LICENSE.txt`).

ZandroX diverges from upstream Zandronum specifically to be cleanly GPL-licensable —
so games built on it can be shipped as commercial open source. To that end the
non-GPL-compatible pieces have been removed or replaced:

- **FMOD** (proprietary) → **OpenAL** + libsndfile/libmpg123 (GPL-compatible, native arm64).
- **Ken Silverman BUILD-engine code** (`BUILDLIC.TXT`, non-GPL): the software renderer
  (`wallscan`, voxel slab drawing, Polymost) is removed — ZandroX is **OpenGL-only**.
- **Fixed-point math** (was BUILD-licensed) → an independent clean-room reimplementation.
- **OPL synth**: `fmopl.cpp` adopts MAME's GPL-2.0+ relicense; Vladimír Arnošt's
  commercial-clause OPL player → Marisa Heit's clean GPL-3.0 rewrite.

The base ZDoom code is the version relicensed to GPL by the ZDoom/GZDoom project;
Zandronum's own additions are under a GPL-compatible BSD-3-clause-style license
(`src/zandronum/LICENSE.txt`); bundled third-party dependencies retain their own
GPL-compatible licenses.

> **Note:** This describes the engineering state, not legal advice. Confirming the base
> ZDoom/Zandronum contributor relicensing and each bundled dependency's terms for a specific
> commercial release is a matter for legal review.
