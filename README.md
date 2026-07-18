# ZandroX

**ZandroX** is a fork of the [Zandronum](https://zandronum.com) source port — a work-in-progress *advanced Zandronum client*. This repo is the macOS build harness: it fetches the Zandronum source, builds it (with FMOD audio + Opus VoIP), and packages a self-contained **`ZandroX.app`** you can double-click.

The build system is based on [rc4l/zandronum-macos-compile](https://github.com/rc4l/zandronum-macos-compile).

## Requirements
- Xcode command line tools — `xcode-select --install`
- [Homebrew](https://brew.sh)
- Git and (installed automatically by the script) Mercurial

## How to Build
```bash
cd ZandroX
./build.sh
```
The first run takes a while — it clones the Zandronum source into `src/zandronum`, builds the x86_64 dependencies from source, downloads FMOD, then compiles. When it's done you'll have a runnable **`build/ZandroX.app`** (`open build/ZandroX.app`).

Make your code changes in **`src/zandronum`** (never in `build/`) and rerun `./build.sh` to rebuild.

### Build modes
- `./build.sh` — full build with FMOD audio. FMOD Ex has no Apple Silicon release, so this compiles for x86_64 and runs under Rosetta 2 (installed automatically if missing).
- `SOUND=0 ./build.sh` — native build (arm64 on Apple Silicon) **without** in-game audio.
- `ARCH=arm64 ./build.sh` — force a specific architecture.

## When to rerun `./build.sh`
- **Test a change:** don't delete anything, just rerun.
- **Clean reinstall, keep code changes:** delete `deps/` and `build/`, then rerun.
- **Wipe everything including code changes:** delete `deps/`, `build/`, and `src/zandronum`, then rerun.

## Layout
- `build/` — compiled output. `build/ZandroX.app` is the self-contained app.
- `deps/` — downloaded/built libraries (FMOD, SDL, OpenSSL, etc). Rarely touched.
- `src/zandronum` — the Zandronum source. **Edit ZandroX code here.**
- `tools/` — bundled build assets (Freedoom WADs, Opus source, icon generator).

## Roadmap
ZandroX starts as an unmodified Zandronum build and grows from there. Planned/likely directions: client-side quality-of-life and UI improvements, then deeper client features. In-game branding is still "Zandronum" for now; the app bundle is branded ZandroX.

## License
ZandroX inherits Zandronum's licensing (see `LICENSE.txt`). Zandronum and all third-party dependencies retain their original licenses.
