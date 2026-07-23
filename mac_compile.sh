#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l

#
# ZandroX macOS Build  (fork of Zandronum)
#
# Builds ZandroX from the Zandronum source tree and packages it as ZandroX.app.
# macOS counterpart to build.ps1. Terminal-only, "run and go".
#
#   ./mac_compile.sh                 # native build with OpenAL audio (default)
#   SOUND=0 ./mac_compile.sh         # native build, no audio (faster)
#   ARCH=x86_64 ./mac_compile.sh     # force a specific architecture
#
# Layout mirrors the Windows build: src/zandronum (source), deps/ (downloads),
# build/ (output).  Source is NEVER patched (touchless rule).
#
# Audio is OpenAL (openal-soft) + libsndfile + libmpg123 — all open-source and
# native on Apple Silicon, so the default build is a real arm64 binary with NO
# FMOD and NO Rosetta. FMOD Ex has been fully removed (it was closed-source,
# abandoned, and x86_64-only, which used to force the whole build through Rosetta).
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

DEFAULT_ZANDRONUM_REF="${ZANDRONUM_REF:-ZA_3.2.1}"
CONFIGURATION="${CONFIGURATION:-Release}"

# SDL is built from source (SDL2 + sdl12-compat 1.2.68). Homebrew's sdl12-compat
# now targets SDL3 and dlopens it by leaf name, which does not survive being
# copied+re-signed into a self-contained .app (its dllinit errors out). The
# from-source sdl12-compat 1.2.68 uses SDL2 and bundles cleanly.
SDL2_URL="https://github.com/libsdl-org/SDL/releases/download/release-2.30.10/SDL2-2.30.10.tar.gz"
SDL12_URL="https://github.com/libsdl-org/sdl12-compat/archive/refs/tags/release-1.2.68.tar.gz"
SDL_PREFIX="$SCRIPT_ROOT/deps/sdl"          # install prefix for from-source SDL
SDL_SRC="$SCRIPT_ROOT/deps/sdlsrc"          # scratch dir for SDL source trees

# macOS .app bundle that build/ ships in addition to the loose binary.
APP_NAME="ZandroX"
BUNDLE_ID="org.zandrox.zandrox"

HOST_ARCH="$(uname -m)"                 # arm64 | x86_64
WANT_SOUND="${SOUND:-1}"                # 1 = OpenAL audio (default), 0 = no audio

# Target architecture: explicit ARCH wins; otherwise native. OpenAL/sndfile/mpg123
# are all native on arm64, so there's no longer any reason to force x86_64.
TARGET_ARCH="${ARCH:-$HOST_ARCH}"

NCPU="$(sysctl -n hw.ncpu)"

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

# Tools needed regardless of path.
ensure_base_tools() {
    status "Installing base tools via Homebrew..."
    local need=()
    for p in mercurial pkg-config cmake; do
        brew list --versions "$p" >/dev/null 2>&1 || need+=("$p")
    done
    (( ${#need[@]} )) && brew install "${need[@]}" || echo "Base tools present."
}

# Dependencies come straight from Homebrew (native host bottles). openal-soft,
# libsndfile and mpg123 are keg-only but that's fine — CMake is pointed at their
# opt/ prefixes, and the .app bundler resolves their absolute install ids.
install_native_deps() {
    status "Installing dependencies via Homebrew..."
    # SDL is built from source (see build_sdl_from_source); everything else comes
    # from Homebrew — none of these dlopen a sibling by leaf name, so they bundle
    # cleanly with the recursive @loader_path pass.
    local pkgs=(glew openssl@3 opus) need=()
    if [[ "$WANT_SOUND" == "1" ]]; then
        pkgs+=(openal-soft libsndfile mpg123)
    fi
    for p in "${pkgs[@]}"; do
        brew list --versions "$p" >/dev/null 2>&1 || need+=("$p")
    done
    (( ${#need[@]} )) && brew install "${need[@]}" || echo "Deps present."
}

# ---------------------------------------------------------------------------
# SDL from source (SDL2 dylib + sdl12-compat dylib), built for TARGET_ARCH.
# sdl12-compat provides the SDL 1.2 API Zandronum links; it dlopens libSDL2 at
# runtime. Both carry absolute install ids so the binary links them and the .app
# bundler can copy + @loader_path them. (This is the piece Homebrew's SDL3-based
# sdl12-compat couldn't do — its dllinit errors when re-signed into a bundle.)
# ---------------------------------------------------------------------------
_sdl_fetch() { [[ -f "$SDL_SRC/$2" ]] || curl -L --fail -o "$SDL_SRC/$2" "$1"; }

build_sdl_from_source() {
    if [[ -f "$SDL_PREFIX/lib/libSDL-1.2.0.dylib" && -f "$SDL_PREFIX/lib/libSDL2-2.0.0.dylib" ]]; then
        echo "SDL already built at $SDL_PREFIX"; return
    fi
    status "Building SDL from source (SDL2 + sdl12-compat, arch: $TARGET_ARCH)..."
    mkdir -p "$SDL_SRC" "$SDL_PREFIX"
    _sdl_fetch "$SDL2_URL"  SDL2.tar.gz
    _sdl_fetch "$SDL12_URL" sdl12compat.tar.gz
    ( cd "$SDL_SRC" && tar xzf SDL2.tar.gz && tar xzf sdl12compat.tar.gz )

    status "  building SDL2 $TARGET_ARCH (dylib)..."
    ( cd "$SDL_SRC"/SDL2-* && \
      cmake -S . -B b "${CMAKE_COMPAT[@]}" -DCMAKE_OSX_ARCHITECTURES="$TARGET_ARCH" \
        -DCMAKE_BUILD_TYPE=Release -DSDL_STATIC=OFF -DSDL_SHARED=ON -DSDL_TEST=OFF \
        -DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" >/dev/null && \
      cmake --build b --parallel "$NCPU" >/dev/null && cmake --install b >/dev/null )

    status "  building sdl12-compat $TARGET_ARCH (dylib)..."
    ( cd "$SDL_SRC"/sdl12-compat-* && \
      cmake -S . -B b "${CMAKE_COMPAT[@]}" -DCMAKE_OSX_ARCHITECTURES="$TARGET_ARCH" \
        -DCMAKE_BUILD_TYPE=Release -DSDL12TESTS=OFF -DCMAKE_PREFIX_PATH="$SDL_PREFIX" \
        -DSDL2_INCLUDE_DIR="$SDL_PREFIX/include/SDL2" \
        -DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" >/dev/null && \
      cmake --build b --parallel "$NCPU" >/dev/null && cmake --install b >/dev/null )

    # Absolute install ids so the linked binary resolves the SDL dylibs at runtime
    # (and the bundler can rewrite them to @loader_path).
    install_name_tool -id "$SDL_PREFIX/lib/libSDL-1.2.0.dylib" "$SDL_PREFIX/lib/libSDL-1.2.0.dylib" 2>/dev/null || true
    install_name_tool -id "$SDL_PREFIX/lib/libSDL2-2.0.0.dylib" "$SDL_PREFIX/lib/libSDL2-2.0.0.dylib" 2>/dev/null || true
    [[ -f "$SDL_PREFIX/lib/libSDL-1.2.0.dylib" ]] || die "SDL build failed."
}

# ---------------------------------------------------------------------------
# Source
# ---------------------------------------------------------------------------
get_source() {
    # Vendored source: this repo tracks src/zandronum directly (git subtree, based
    # on ZA_3.2.1). Treat a present, non-hg tree as authoritative so the build
    # never clobbers local changes. Set ZANDRONUM_FETCH=1 to force a fresh pull.
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
    local glew ssl
    glew="$(brew --prefix glew)"
    ssl="$(brew --prefix openssl@3)"

    local args=(
        -S "$ZAN_SRC_DIR" -B "$BUILD_DIR" "${CMAKE_COMPAT[@]}"
        -DCMAKE_BUILD_TYPE="$CONFIGURATION"
        -DCMAKE_OSX_ARCHITECTURES="$TARGET_ARCH"
        -DCMAKE_EXE_LINKER_FLAGS="$APPLE_FRAMEWORKS"
        # macOS has no system libjpeg; force the bundled jpeg-6b so find_package
        # can't latch onto a stray Homebrew libjpeg and break the link.
        -DFORCE_INTERNAL_JPEG=ON
        # FMOD is gone; OpenAL is the only audio backend.
        -DNO_FMOD=ON
        # [rc4l] Native SDL2, not the sdl12-compat shim: SDL 1.2 cannot request a GL version/profile.
        -DSDL2_DIR="$SDL_PREFIX/lib/cmake/SDL2"
        -DGLEW_INCLUDE_DIR="$glew/include"
        -DOPENSSL_ROOT_DIR="$ssl"
    )

    # [rc4l] Vulkan backend support (ZVulkan): opt in only when the brew deps are present
    # (molten-vk vulkan-headers vulkan-loader). ZVulkan's CMake on Apple requires MoltenVK;
    # the brew kegs are keg-only, so point FindVulkan at them explicitly.
    local mvk vkh vkl
    mvk="$(brew --prefix molten-vk 2>/dev/null || true)"
    vkh="$(brew --prefix vulkan-headers 2>/dev/null || true)"
    vkl="$(brew --prefix vulkan-loader 2>/dev/null || true)"
    if [[ -f "$mvk/lib/libMoltenVK.dylib" && -d "$vkh/include/vulkan" && -f "$vkl/lib/libvulkan.dylib" ]]; then
        status "  Vulkan deps found (MoltenVK); building ZVulkan (-DHAVE_VULKAN=ON)."
        args+=(
            -DHAVE_VULKAN=ON
            -DVulkan_INCLUDE_DIR="$vkh/include"
            -DVulkan_LIBRARY="$vkl/lib/libvulkan.dylib"
            -DVulkan_MoltenVK_LIBRARY="$mvk/lib/libMoltenVK.dylib"
        )
    else
        warn "Vulkan deps not found (brew install molten-vk vulkan-headers vulkan-loader); building without ZVulkan."
    fi

    if [[ "$WANT_SOUND" == "1" ]]; then
        local oal snd mp3
        oal="$(brew --prefix openal-soft)"; snd="$(brew --prefix libsndfile)"; mp3="$(brew --prefix mpg123)"
        # find_package(OpenAL) prefers the deprecated system OpenAL.framework, which
        # is OpenAL 1.1 and lacks alext.h/EFX. Point it at openal-soft explicitly.
        args+=(
            -DNO_OPENAL=OFF
            -DOPENAL_INCLUDE_DIR="$oal/include/AL"
            -DOPENAL_LIBRARY="$oal/lib/libopenal.dylib"
            -DCMAKE_PREFIX_PATH="$oal;$snd;$mp3;$ssl"
        )
        # libsndfile/mpg123 are found via pkg-config by FindSndFile/FindMPG123.
        export PKG_CONFIG_PATH="$snd/lib/pkgconfig:$mp3/lib/pkgconfig:$oal/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    else
        warn "Building WITHOUT sound (-DNO_SOUND=ON)."
        args+=( -DNO_SOUND=ON )
    fi
    cmake "${args[@]}"
}

build() {
    status "Building Zandronum..."
    cmake --build "$BUILD_DIR" --config "$CONFIGURATION" --parallel "$NCPU"

    # Freedoom WADs for a runnable game (matches the Windows build).
    # [rc4l] Freedoom is BSD-3-clause: clause 2 requires its copyright notice to travel with
    # any binary distribution, so the license ships beside the WAD rather than just the WAD.
    if [[ -f "$TOOLS_DIR/freedoom/freedoom2.wad" ]]; then
        cp -n "$TOOLS_DIR/freedoom/"*.wad "$BUILD_DIR/" 2>/dev/null || true
        cp -f "$TOOLS_DIR/freedoom/License.txt" "$BUILD_DIR/FREEDOOM-LICENSE.txt" 2>/dev/null || true
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
        # (Guard the array expansion: macOS bash 3.2 errors on ${arr[*]} for an
        # empty array under `set -u`.)
        if (( ${#_BUNDLED[@]} )) && [[ " ${_BUNDLED[*]} " == *" $leaf "* ]]; then
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
    # [rc4l] Freedoom's BSD-3-clause license must travel with the WAD it covers, and GPL-3.0
    # sections 4-6 require the license text plus a pointer to the corresponding source; a
    # binary without them is not compliant. Missing files must not abort the build.
    cp -f "$BUILD_DIR/FREEDOOM-LICENSE.txt" "$macos/" 2>/dev/null || true
    cp -f "$SCRIPT_ROOT/LICENSE.txt" "$macos/" 2>/dev/null || true
    cp -f "$SCRIPT_ROOT/THIRD-PARTY-NOTICES.txt" "$macos/" 2>/dev/null || true

    # Stage and re-point dylibs. The recursive pass follows the link graph (OpenAL,
    # sndfile, mpg123, opus, sdl12-compat's libSDL-1.2, GLEW, openssl all resolve
    # via their absolute install ids). sdl12-compat dlopens libSDL2 by leaf name at
    # runtime (no link-time edge), so stage it explicitly at @loader_path next to
    # the binary, where sdl12-compat's dlopen looks first.
    _BUNDLED=()
    local search=("$(brew --prefix)/lib" "$SDL_PREFIX/lib" "$macos")
    if [[ -f "$SDL_PREFIX/lib/libSDL2-2.0.0.dylib" ]]; then
        cp -L "$SDL_PREFIX/lib/libSDL2-2.0.0.dylib" "$macos/" && chmod u+w "$macos/libSDL2-2.0.0.dylib"
        _BUNDLED+=("libSDL2-2.0.0.dylib")
        install_name_tool -id "@loader_path/libSDL2-2.0.0.dylib" "$macos/libSDL2-2.0.0.dylib" 2>/dev/null || true
    else
        warn "libSDL2-2.0.0.dylib not found in $SDL_PREFIX; run build_sdl_from_source"
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
    # accepts the Mach-Os.  Sign the dylibs first, then deep-sign the bundle to
    # seal the main executable.
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
    install_native_deps
    build_sdl_from_source
    configure
    build
    make_app_bundle
    show_results
    status "Done."
}

main "$@"
