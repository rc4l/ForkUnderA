#!/usr/bin/env bash
#
# ZandroX macOS Build  (fork of Zandronum)
#
# Builds ZandroX from the Zandronum source tree and packages it as ZandroX.app.
# macOS counterpart to build.ps1. Terminal-only, "run and go".
#
#   ./build.sh                 # full build with FMOD audio (default)
#   SOUND=0 ./build.sh         # native build, no audio (faster; native arm64 on Apple Silicon)
#   ARCH=arm64 ./build.sh      # force a specific architecture
#
# Layout mirrors the Windows build: src/zandronum (source), deps/ (downloads),
# build/ (output).  Source is NEVER patched (touchless rule).
#
# Two build modes (both proven on an M1 Max, 2026-06-26, untouched ZA_3.2.1):
#
#   DEFAULT (sound on)  -- arch = x86_64, FULL FMOD audio + Opus VoIP
#     FMOD Ex 4.44.64 ships x86_64/i386 only (no arm64, closed-source, abandoned),
#     so the audio build is x86_64; on Apple Silicon it runs under Rosetta 2
#     (installed automatically if missing). The x86_64 deps are built from source
#     into deps/x86 (Homebrew bottles are arm64-only), FMOD is linked, and the
#     runtime dylibs are staged next to the binary.
#
#   SOUND=0  -- arch = host, deps from Homebrew, but WITHOUT audio (-DNO_SOUND=ON)
#     On Apple Silicon this gives a native arm64 binary.
set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
SCRIPT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPS_DIR="$SCRIPT_ROOT/deps"
SRC_DIR="$SCRIPT_ROOT/src"
BUILD_DIR="$SCRIPT_ROOT/build"
TOOLS_DIR="$SCRIPT_ROOT/tools"
ZAN_SRC_DIR="$SRC_DIR/zandronum"
X86_PREFIX="$DEPS_DIR/x86"            # install prefix for from-source x86_64 deps
X86_SRC="$DEPS_DIR/x86src"            # scratch dir for x86_64 dep source trees

DEFAULT_ZANDRONUM_REF="${ZANDRONUM_REF:-ZA_3.2.1}"
CONFIGURATION="${CONFIGURATION:-Release}"

# macOS .app bundle that build/ ships in addition to the loose binary.
APP_NAME="ZandroX"
BUNDLE_ID="org.zandrox.zandrox"

HOST_ARCH="$(uname -m)"                 # arm64 | x86_64
WANT_SOUND="${SOUND:-1}"                # 0 = native build without FMOD audio

# Decide target architecture: explicit ARCH wins; SOUND=1 forces x86_64; else native.
if [[ -n "${ARCH:-}" ]]; then
    TARGET_ARCH="$ARCH"
elif [[ "$WANT_SOUND" == "1" ]]; then
    TARGET_ARCH="x86_64"
else
    TARGET_ARCH="$HOST_ARCH"
fi
[[ "$WANT_SOUND" == "1" && "$TARGET_ARCH" != "x86_64" ]] && \
    { echo "ERROR: SOUND=1 requires ARCH=x86_64 (FMOD Ex has no arm64 build)." >&2; exit 1; }

NCPU="$(sysctl -n hw.ncpu)"

# Dependency versions (from-source x86_64 path)
SDL2_URL="https://github.com/libsdl-org/SDL/releases/download/release-2.30.10/SDL2-2.30.10.tar.gz"
SDL12_URL="https://github.com/libsdl-org/sdl12-compat/archive/refs/tags/release-1.2.68.tar.gz"
GLEW_URL="https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.tgz"
OPENSSL_URL="https://github.com/openssl/openssl/releases/download/openssl-3.5.1/openssl-3.5.1.tar.gz"
FMOD_DMG_URL="https://zdoom.org/files/fmod/fmodapi44464mac-installer.dmg"

# CMake 4.x dropped support for Zandronum's old cmake_minimum_required.
CMAKE_COMPAT=(-DCMAKE_POLICY_VERSION_MINIMUM=3.5)
# Apple frameworks Zandronum's CMake does not auto-link.
APPLE_FRAMEWORKS="-framework CoreFoundation -framework Carbon -framework Cocoa -framework IOKit -framework OpenGL"

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------
status()  { printf '\033[32m==> %s\033[0m\n' "$*"; }
warn()    { printf '\033[33mWARNING: %s\033[0m\n' "$*"; }
die()     { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
have()    { command -v "$1" >/dev/null 2>&1; }

# ---------------------------------------------------------------------------
# Toolchain
# ---------------------------------------------------------------------------
ensure_xcode() {
    status "Checking Xcode command line tools..."
    xcode-select -p >/dev/null 2>&1 || die "Xcode CLT not found. Run: xcode-select --install"
    have clang || die "clang not found."
}

ensure_homebrew() {
    status "Checking Homebrew..."
    have brew || die "Homebrew not found. Install from https://brew.sh then re-run."
}

ensure_rosetta() {
    [[ "$HOST_ARCH" == "arm64" ]] || return 0
    status "Ensuring Rosetta 2 (needed to run the x86_64 build)..."
    if ! /usr/bin/pgrep oahd >/dev/null 2>&1; then
        warn "Rosetta not detected; attempting install (may require sudo)..."
        softwareupdate --install-rosetta --agree-to-license || \
            die "Could not install Rosetta. Run: softwareupdate --install-rosetta"
    fi
    arch -x86_64 /usr/bin/true 2>/dev/null || die "x86_64 execution unavailable even with Rosetta."
}

# Tools needed regardless of path (built deps still use cmake/hg).
ensure_base_tools() {
    status "Installing base tools via Homebrew..."
    local need=()
    for p in mercurial pkg-config cmake; do
        brew list --versions "$p" >/dev/null 2>&1 || need+=("$p")
    done
    (( ${#need[@]} )) && brew install "${need[@]}" || echo "Base tools present."
}

# Native-path deps come straight from Homebrew (arm64 or x86_64 host bottles).
install_native_deps() {
    status "Installing native dependencies via Homebrew..."
    # Note: no FluidSynth -- the Windows build doesn't ship it either; MIDI uses
    # the built-in OPL synth. FMOD/Opus provide all required audio.
    local pkgs=(sdl12-compat glew openssl@3 opus) need=()
    for p in "${pkgs[@]}"; do
        brew list --versions "$p" >/dev/null 2>&1 || need+=("$p")
    done
    (( ${#need[@]} )) && brew install "${need[@]}" || echo "Native deps present."
}

# ---------------------------------------------------------------------------
# x86_64 dependencies built from source (SOUND=1 path)
# Homebrew bottles are arm64-only on Apple Silicon and cannot link into an
# x86_64 binary, so we cross-compile these with clang -arch x86_64.
# ---------------------------------------------------------------------------
_fetch() { [[ -f "$X86_SRC/$2" ]] || curl -L --fail -o "$X86_SRC/$2" "$1"; }

build_x86_deps() {
    if [[ -f "$X86_PREFIX/lib/libSDL-1.2.0.dylib" && -f "$X86_PREFIX/lib/libopus.a" \
          && -f "$X86_PREFIX/lib/libssl.a" && -f "$X86_PREFIX/lib/libGLEW.a" ]]; then
        echo "x86_64 deps already built at $X86_PREFIX"; return
    fi
    status "Building x86_64 dependencies from source (Rosetta path)..."
    mkdir -p "$X86_SRC" "$X86_PREFIX"

    _fetch "$OPENSSL_URL" openssl.tar.gz
    _fetch "$SDL2_URL"    SDL2.tar.gz
    _fetch "$SDL12_URL"   sdl12compat.tar.gz
    _fetch "$GLEW_URL"    glew.tgz
    ( cd "$X86_SRC" && for f in openssl SDL2 sdl12compat; do tar xzf $f.tar.gz; done && tar xzf glew.tgz )
    # Opus source ships committed in the repo.
    ( cd "$X86_SRC" && tar xzf "$TOOLS_DIR/opus/"opus-*.tar.gz )

    # --- OpenSSL (static) ---
    status "  building OpenSSL x86_64 (static)..."
    ( cd "$X86_SRC"/openssl-* && \
      ./Configure darwin64-x86_64-cc no-shared no-tests --prefix="$X86_PREFIX" --openssldir="$X86_PREFIX/ssl" >/dev/null && \
      make -j"$NCPU" >/dev/null && make install_sw >/dev/null )

    # --- Opus (static) ---
    status "  building Opus x86_64 (static)..."
    ( cd "$X86_SRC"/opus-* && \
      cmake -S . -B b "${CMAKE_COMPAT[@]}" -DCMAKE_OSX_ARCHITECTURES=x86_64 \
        -DCMAKE_BUILD_TYPE=Release -DOPUS_BUILD_SHARED_LIBRARY=OFF \
        -DCMAKE_INSTALL_PREFIX="$X86_PREFIX" >/dev/null && \
      cmake --build b --parallel "$NCPU" >/dev/null && cmake --install b >/dev/null )

    # --- GLEW (static) ---
    status "  building GLEW x86_64 (static)..."
    ( cd "$X86_SRC"/glew-* && \
      cmake -S build/cmake -B b "${CMAKE_COMPAT[@]}" -DCMAKE_OSX_ARCHITECTURES=x86_64 \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_INSTALL_PREFIX="$X86_PREFIX" >/dev/null && \
      cmake --build b --parallel "$NCPU" >/dev/null && cmake --install b >/dev/null )

    # --- SDL2 (dylib) ---
    status "  building SDL2 x86_64 (dylib)..."
    ( cd "$X86_SRC"/SDL2-* && \
      cmake -S . -B b "${CMAKE_COMPAT[@]}" -DCMAKE_OSX_ARCHITECTURES=x86_64 \
        -DCMAKE_BUILD_TYPE=Release -DSDL_STATIC=OFF -DSDL_SHARED=ON -DSDL_TEST=OFF \
        -DCMAKE_INSTALL_PREFIX="$X86_PREFIX" >/dev/null && \
      cmake --build b --parallel "$NCPU" >/dev/null && cmake --install b >/dev/null )

    # --- sdl12-compat (dylib, provides the SDL 1.2 API Zandronum links) ---
    status "  building sdl12-compat x86_64 (dylib)..."
    ( cd "$X86_SRC"/sdl12-compat-* && \
      cmake -S . -B b "${CMAKE_COMPAT[@]}" -DCMAKE_OSX_ARCHITECTURES=x86_64 \
        -DCMAKE_BUILD_TYPE=Release -DSDL12TESTS=OFF -DCMAKE_PREFIX_PATH="$X86_PREFIX" \
        -DSDL2_INCLUDE_DIR="$X86_PREFIX/include/SDL2" \
        -DCMAKE_INSTALL_PREFIX="$X86_PREFIX" >/dev/null && \
      cmake --build b --parallel "$NCPU" >/dev/null && cmake --install b >/dev/null )

    # Absolute install ids so the linked binary resolves the SDL dylibs at runtime.
    install_name_tool -id "$X86_PREFIX/lib/libSDL-1.2.0.dylib" "$X86_PREFIX/lib/libSDL-1.2.0.dylib"
    install_name_tool -id "$X86_PREFIX/lib/libSDL2-2.0.0.dylib" "$X86_PREFIX/lib/libSDL2-2.0.0.dylib"
}

# ---------------------------------------------------------------------------
# FMOD (x86_64 only)
# ---------------------------------------------------------------------------
get_fmod() {
    local fmod_dir="$DEPS_DIR/fmod"
    [[ -f "$fmod_dir/lib/libfmodex.dylib" ]] && { echo "FMOD already staged."; return; }
    status "Staging FMOD Ex 4.44.64..."
    mkdir -p "$DEPS_DIR"
    local dmg="$DEPS_DIR/fmodmac.dmg"
    [[ -f "$dmg" ]] || curl -L --fail -o "$dmg" "$FMOD_DMG_URL"
    local mnt; mnt="$(mktemp -d)"
    hdiutil attach "$dmg" -nobrowse -quiet -mountpoint "$mnt"
    # 2>/dev/null + || true: a freshly mounted dmg has a .Trashes dir the runner
    # user can't read, which makes find exit non-zero and trip `set -e`.
    local api; api="$(find "$mnt" -maxdepth 3 -type d -name api 2>/dev/null | head -1 || true)"
    [[ -n "$api" ]] || { hdiutil detach "$mnt" -quiet; die "FMOD api/ dir not found in dmg."; }
    mkdir -p "$fmod_dir"; cp -R "$api/inc" "$fmod_dir/include"; cp -R "$api/lib" "$fmod_dir/lib"
    hdiutil detach "$mnt" -quiet
    [[ -f "$fmod_dir/lib/libfmodex.dylib" ]] || die "FMOD staging failed."
}

# ---------------------------------------------------------------------------
# Source
# ---------------------------------------------------------------------------
get_source() {
    # Vendored source: this repo now tracks src/zandronum directly (git subtree,
    # based on ZA_3.2.1). Treat a present, non-hg tree as authoritative so the
    # build never clobbers local changes. Set ZANDRONUM_FETCH=1 to force a fresh
    # upstream pull instead (restores the old hg clone/update behavior).
    if [[ "${ZANDRONUM_FETCH:-0}" != "1" && -f "$ZAN_SRC_DIR/CMakeLists.txt" && ! -d "$ZAN_SRC_DIR/.hg" ]]; then
        status "Using vendored Zandronum source in $ZAN_SRC_DIR (set ZANDRONUM_FETCH=1 to re-fetch)."
        return
    fi

    status "Setting up Zandronum source (ref: $DEFAULT_ZANDRONUM_REF)..."
    mkdir -p "$SRC_DIR"
    if [[ -d "$ZAN_SRC_DIR/.hg" ]]; then
        ( cd "$ZAN_SRC_DIR" && hg pull && hg update "$DEFAULT_ZANDRONUM_REF" )
    else
        rm -rf "$ZAN_SRC_DIR"
        hg clone https://foss.heptapod.net/zandronum/zandronum-stable "$ZAN_SRC_DIR"
        ( cd "$ZAN_SRC_DIR" && hg update "$DEFAULT_ZANDRONUM_REF" )
    fi
    [[ -f "$ZAN_SRC_DIR/CMakeLists.txt" ]] || die "CMakeLists.txt missing; source corrupt."
}

# ---------------------------------------------------------------------------
# Configure + build
# ---------------------------------------------------------------------------
configure() {
    status "Configuring with CMake (arch: $TARGET_ARCH, sound: $WANT_SOUND)..."
    local args=(
        -S "$ZAN_SRC_DIR" -B "$BUILD_DIR" "${CMAKE_COMPAT[@]}"
        -DCMAKE_BUILD_TYPE="$CONFIGURATION"
        -DCMAKE_OSX_ARCHITECTURES="$TARGET_ARCH"
        -DCMAKE_EXE_LINKER_FLAGS="$APPLE_FRAMEWORKS"
        # macOS has no system libjpeg, so find_package(JPEG) can latch onto a stray
        # arm64 Homebrew libjpeg and fail the x86_64 link with undefined symbols.
        # Force the bundled jpeg-6b instead. (zlib/bzip2 resolve to the universal
        # system libs and link fine, so they're left alone.)
        -DFORCE_INTERNAL_JPEG=ON
    )

    if [[ "$WANT_SOUND" == "1" ]]; then
        get_fmod
        args+=(
            -DSDL_INCLUDE_DIR="$X86_PREFIX/include/SDL"
            -DSDL_LIBRARY="$X86_PREFIX/lib/libSDL-1.2.0.dylib"
            -DGLEW_INCLUDE_DIR="$X86_PREFIX/include"
            -DGLEW_LIBRARY="$X86_PREFIX/lib/libGLEW.a"
            -DOPENSSL_ROOT_DIR="$X86_PREFIX" -DOPENSSL_USE_STATIC_LIBS=ON
            -DOPUS_INCLUDE_DIR="$X86_PREFIX/include/opus"
            -DOPUS_LIBRARIES="$X86_PREFIX/lib/libopus.a"
            -DFMOD_INCLUDE_DIR="$DEPS_DIR/fmod/include"
            -DFMOD_LIBRARY="$DEPS_DIR/fmod/lib/libfmodex.dylib"
        )
    else
        warn "Building WITHOUT sound (-DNO_SOUND=ON). FMOD has no $TARGET_ARCH build."
        local sdl glew ssl opus
        sdl="$(brew --prefix sdl12-compat)"; glew="$(brew --prefix glew)"
        ssl="$(brew --prefix openssl@3)"
        args+=(
            -DNO_SOUND=ON
            -DSDL_INCLUDE_DIR="$sdl/include/SDL"
            -DSDL_LIBRARY="$sdl/lib/libSDL-1.2.0.dylib"
            -DGLEW_INCLUDE_DIR="$glew/include"
            -DOPENSSL_ROOT_DIR="$ssl"
        )
    fi
    cmake "${args[@]}"
}

build() {
    status "Building Zandronum..."
    cmake --build "$BUILD_DIR" --config "$CONFIGURATION" --parallel "$NCPU"

    # Freedoom WADs for a runnable game (matches the Windows build).
    [[ -f "$TOOLS_DIR/freedoom/freedoom2.wad" ]] && cp -n "$TOOLS_DIR/freedoom/"*.wad "$BUILD_DIR/" 2>/dev/null || true

    if [[ "$WANT_SOUND" == "1" ]]; then
        status "Staging runtime dylibs next to the binary..."
        cp "$DEPS_DIR/fmod/lib/libfmodex.dylib" "$BUILD_DIR/"
        cp "$X86_PREFIX/lib/libSDL2-2.0.0.dylib" "$BUILD_DIR/"
        # FMOD's install id is "./libfmodex.dylib"; make it CWD-independent.
        install_name_tool -change ./libfmodex.dylib @loader_path/libfmodex.dylib "$BUILD_DIR/zandronum" 2>/dev/null || true
    fi
}

# ---------------------------------------------------------------------------
# .app bundle
#
# Zandronum's SDL build sets progdir from the executable's own directory
# (realpath of argv[0]), and that's where it looks for its pk3/wad data.  So a
# self-contained bundle is just: binary + data + dylibs together under
# Contents/MacOS/, with every non-system dylib reference rewritten to
# @loader_path so the app runs after being moved or copied to another Mac.
# ---------------------------------------------------------------------------

# Resolve a dylib install-name (which may be absolute, @loader_path/..., or a
# bare leafname) to an actual file on disk, searching a list of known dirs.
# Echoes the resolved path, or nothing if it can't be found.
_resolve_dylib() {
    local ref="$1"; shift
    local leaf="${ref##*/}"
    [[ "$ref" == /* && -f "$ref" ]] && { echo "$ref"; return; }
    local dir
    for dir in "$@"; do
        [[ -f "$dir/$leaf" ]] && { echo "$dir/$leaf"; return; }
    done
}

# Recursively copy a Mach-O's non-system dynamic dependencies into the bundle's
# MacOS dir and repoint every reference (and each copied dylib's own id) at
# @loader_path.  System libs (/usr/lib, /System) are left untouched.
declare -a _BUNDLED=()
_bundle_deps() {
    local macho="$1" macos_dir="$2"; shift 2
    local search=("$@")
    local ref leaf src
    while IFS= read -r ref; do
        [[ "$ref" == /usr/lib/* || "$ref" == /System/* ]] && continue
        [[ "$ref" == "$(otool -D "$macho" | tail -n +2 | head -1)" ]] && continue  # skip self id
        leaf="${ref##*/}"
        # Already staged this dylib? Just fix the referrer and move on.
        if [[ " ${_BUNDLED[*]} " == *" $leaf "* ]]; then
            install_name_tool -change "$ref" "@loader_path/$leaf" "$macho" 2>/dev/null || true
            continue
        fi
        src="$(_resolve_dylib "$ref" "${search[@]}")" || true
        [[ -n "$src" ]] || { warn "could not resolve dylib '$ref' for bundling"; continue; }
        cp -L "$src" "$macos_dir/$leaf"
        chmod u+w "$macos_dir/$leaf"
        _BUNDLED+=("$leaf")
        install_name_tool -id "@loader_path/$leaf" "$macos_dir/$leaf" 2>/dev/null || true
        install_name_tool -change "$ref" "@loader_path/$leaf" "$macho" 2>/dev/null || true
        _bundle_deps "$macos_dir/$leaf" "$macos_dir" "${search[@]}"   # transitive deps
    done < <(otool -L "$macho" | tail -n +2 | awk '{print $1}')
}

make_icon() {
    local resources="$1"
    # Prefer Zandronum's square Windows app icon (a proper dock tile); fall back
    # to the wide wordmark logo if the source tree doesn't have it.
    local logo="$ZAN_SRC_DIR/src/win32/zandronum.ico"
    [[ -f "$logo" ]] || logo="$SCRIPT_ROOT/docs/logo.png"
    have python3 && have iconutil && [[ -f "$logo" ]] || { warn "skipping icon (tooling/logo missing)"; return 0; }
    local iconset; iconset="$(mktemp -d)/$APP_NAME.iconset"; mkdir -p "$iconset"
    if python3 "$TOOLS_DIR/make-iconset.py" "$logo" "$iconset" 2>/dev/null \
       && iconutil -c icns "$iconset" -o "$resources/$APP_NAME.icns" 2>/dev/null; then
        echo "$APP_NAME.icns"          # CFBundleIconFile value
    else
        warn "icon generation failed; bundle will have no custom icon"
    fi
}

make_app_bundle() {
    status "Assembling $APP_NAME.app..."
    local app="$BUILD_DIR/$APP_NAME.app"
    local macos="$app/Contents/MacOS" resources="$app/Contents/Resources"
    rm -rf "$app"
    mkdir -p "$macos" "$resources"

    # Binary + game data live together so progdir (= MacOS dir) finds the data.
    cp "$BUILD_DIR/zandronum" "$macos/zandronum"
    chmod u+w "$macos/zandronum"
    local f
    for f in "$BUILD_DIR"/*.pk3 "$BUILD_DIR"/*.wad; do
        [[ -e "$f" ]] && cp "$f" "$macos/"
    done

    # Stage and re-point dylibs.  In SOUND=1 the deps are the from-source x86_64
    # libs; in SOUND=0 they're Homebrew's.  The recursive pass follows the link
    # graph; sdl12-compat dlopens SDL2 at runtime (no link-time edge), so SDL2
    # is staged explicitly and found via @executable_path next to the binary.
    _BUNDLED=()
    local search
    if [[ "$WANT_SOUND" == "1" ]]; then
        search=("$X86_PREFIX/lib" "$DEPS_DIR/fmod/lib" "$macos")
        cp "$X86_PREFIX/lib/libSDL2-2.0.0.dylib" "$macos/" && chmod u+w "$macos/libSDL2-2.0.0.dylib"
        _BUNDLED+=("libSDL2-2.0.0.dylib")
        install_name_tool -id "@loader_path/libSDL2-2.0.0.dylib" "$macos/libSDL2-2.0.0.dylib" 2>/dev/null || true
    else
        local sdl; sdl="$(brew --prefix sdl12-compat)"
        search=("$(brew --prefix)/lib" "$sdl/lib" "$macos")
        if [[ -f "$sdl/lib/libSDL2-2.0.0.dylib" ]]; then
            cp -L "$sdl/lib/libSDL2-2.0.0.dylib" "$macos/" && chmod u+w "$macos/libSDL2-2.0.0.dylib"
            _BUNDLED+=("libSDL2-2.0.0.dylib")
            install_name_tool -id "@loader_path/libSDL2-2.0.0.dylib" "$macos/libSDL2-2.0.0.dylib" 2>/dev/null || true
        fi
    fi
    _bundle_deps "$macos/zandronum" "$macos" "${search[@]}"

    local icon; icon="$(make_icon "$resources")"

    # Version string from the checked-out Zandronum tag (e.g. ZA_3.2.1 -> 3.2.1).
    local ver="${DEFAULT_ZANDRONUM_REF#ZA_}"
    cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key><string>$APP_NAME</string>
	<key>CFBundleDisplayName</key><string>$APP_NAME</string>
	<key>CFBundleIdentifier</key><string>$BUNDLE_ID</string>
	<key>CFBundleExecutable</key><string>zandronum</string>
	<key>CFBundlePackageType</key><string>APPL</string>
	<key>CFBundleShortVersionString</key><string>$ver</string>
	<key>CFBundleVersion</key><string>$ver</string>
	<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
	<key>LSMinimumSystemVersion</key><string>10.9</string>
	<key>NSHighResolutionCapable</key><true/>
	<key>NSPrincipalClass</key><string>NSApplication</string>${icon:+
	<key>CFBundleIconFile</key><string>$icon</string>}
</dict>
</plist>
PLIST

    # install_name_tool invalidates code signatures; ad-hoc re-sign so the loader
    # accepts the Mach-Os, incl. on Apple Silicon under Rosetta.  Sign the dylibs
    # first, then deep-sign the bundle to seal the main executable.  (Signing the
    # executable on its own trips over the sibling pk3/wad data files, so let the
    # deep bundle sign handle it.)
    if have codesign; then
        for f in "$macos"/*.dylib; do
            [[ -e "$f" ]] && codesign --force --sign - "$f" >/dev/null 2>&1 || true
        done
        codesign --force --deep --sign - "$app" >/dev/null 2>&1 || true
    fi
    status "$APP_NAME.app ready: $app"
}

show_results() {
    status "Build results:"
    local bin="$BUILD_DIR/zandronum"
    if [[ -x "$bin" ]]; then
        echo "  binary: $bin"
        lipo -info "$bin" | sed 's/^/  /'
        ls -lh "$bin" | awk '{print "  size: "$5}'
    else
        warn "zandronum binary not found in $BUILD_DIR"
    fi
    local app="$BUILD_DIR/$APP_NAME.app"
    if [[ -d "$app" ]]; then
        echo "  app bundle: $app  (self-contained; double-click or 'open' it)"
        echo "  run from terminal: open \"$app\""
    fi
    ls -1 "$BUILD_DIR"/*.pk3 2>/dev/null | sed 's/^/  pk3: /' || true
}

# ---------------------------------------------------------------------------
main() {
    status "ZandroX macOS build  (host: $HOST_ARCH, target: $TARGET_ARCH, sound: $WANT_SOUND)"
    ensure_xcode
    ensure_homebrew
    ensure_base_tools
    get_source
    if [[ "$WANT_SOUND" == "1" ]]; then
        ensure_rosetta
        build_x86_deps
    else
        install_native_deps
    fi
    configure
    build
    make_app_bundle
    show_results
    status "Done."
}

main "$@"
