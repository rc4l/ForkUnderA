# ZandroX

macOS build harness for the [Zandronum](https://zandronum.com) source port. Builds Zandronum from source and packages a self-contained **`ZandroX.app`**.

Based on [rc4l/zandronum-macos-compile](https://github.com/rc4l/zandronum-macos-compile).

## Requirements
- Xcode command line tools — `xcode-select --install`
- [Homebrew](https://brew.sh)

## Build
```bash
./build.sh            # full build (FMOD audio; x86_64 under Rosetta 2)
SOUND=0 ./build.sh    # native arm64, no in-game audio
```
Output: **`build/ZandroX.app`** (`open build/ZandroX.app`).

## License
Inherits Zandronum's licensing (see `LICENSE.txt`). Zandronum and all third-party dependencies retain their original licenses.
