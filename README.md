# ZandroX

macOS build harness for the [Zandronum](https://zandronum.com) source port. Builds Zandronum from source and packages a self-contained **`ZandroX.app`**.

Based on [rc4l/zandronum-macos-compile](https://github.com/rc4l/zandronum-macos-compile).

## Requirements
- Xcode command line tools — `xcode-select --install`
- [Homebrew](https://brew.sh)

## Build
```bash
./build.sh            # native arm64, OpenAL audio (default)
SOUND=0 ./build.sh    # native arm64, no in-game audio
```
Output: **`build/ZandroX.app`** (`open build/ZandroX.app`).

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
