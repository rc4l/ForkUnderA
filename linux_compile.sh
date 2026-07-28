#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l

# [rc4l] Compile ZandroX on Linux from the in-repo source — the native, no-Docker path.
#
# A Linux contributor runs this directly to build and package ZandroX. It is also the
# single source of truth for the build: package-linux.sh runs THIS script inside an
# Ubuntu 22.04 container to produce the release tarball with wide glibc compatibility,
# so a dev build and a release build share exactly one code path.
#
# De-Zandronum principle — this is ZandroX, not upstream Zandronum: OpenAL only (never
# FMOD), and it compiles the source already in this repo (src/zandronum) rather than
# downloading Zandronum. FMOD/GTK stay off.
#
#   ./linux_compile.sh                     # full client build + tarball
#   SERVERONLY=ON ./linux_compile.sh       # headless server build
#   VERSION=v0.1.0 ./linux_compile.sh      # stamp a version into the tarball name
#   ./linux_compile.sh --install-deps      # apt-install the build dependencies first (uses sudo)
#   ./linux_compile.sh --no-package        # compile only, skip the tarball
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

SERVERONLY="${SERVERONLY:-OFF}"
VERSION="${VERSION:-}"
NO_PACKAGE=OFF
INSTALL_DEPS=OFF

while [ $# -gt 0 ]; do
  case "$1" in
    --serveronly)   SERVERONLY=ON ;;
    --no-package)   NO_PACKAGE=ON ;;
    --install-deps) INSTALL_DEPS=ON ;;
    --version)      VERSION="${2:-}"; shift ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

# [rc4l] Deps mirror Dockerfile.linux-build so native and containerised builds match:
# OpenSSL/Opus/zlib/bzip2/jpeg + Python3, SDL2 + OpenGL/GLU/GLEW + GME for the client,
# and the OpenAL stack (libopenal + libsndfile + libmpg123) that replaces FMOD.
DEPS=(
  build-essential cmake ninja-build git python3 pkg-config ca-certificates
  libssl-dev libopus-dev zlib1g-dev libbz2-dev libjpeg-dev
  libsdl2-dev libgl1-mesa-dev libglu1-mesa-dev libglew-dev libgme-dev
  libopenal-dev libsndfile1-dev libmpg123-dev
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
  libcurl4-openssl-dev
)

if [ "$INSTALL_DEPS" = "ON" ]; then
  echo "==> Installing build dependencies (apt)"
  # [rc4l] APT_CACHE_DIR (set by CI) is a persistent directory of .deb files. Seed apt's
  # archive dir from it so an install that is already cached downloads nothing, and harvest
  # it afterwards so the next run is seeded too. The debs go through apt's normal root-owned
  # archive dir rather than pointing Dir::Cache::archives at the cache: apt drops privileges
  # to _apt for downloads and warns/degrades on a dir it cannot write.
  #
  # This exists because dependency DOWNLOAD, not compilation, is what makes this job slow.
  # A release build on 2026-07-28 spent 21m50s in apt (multi-minute stalls part-way through
  # the ffmpeg dev tree on azure.archive.ubuntu.com) against 3m27s actually compiling — the
  # compile is ~3.5 min on every run, fast or slow. Caching the debs is what bounds that
  # tail; a compiler cache would target the 3.5 minutes that were never the problem.
  if [ -n "${APT_CACHE_DIR:-}" ]; then
    mkdir -p "$APT_CACHE_DIR"
    sudo cp -n "$APT_CACHE_DIR"/*.deb /var/cache/apt/archives/ 2>/dev/null || true
  fi

  # Retries cover a mirror that drops a connection outright; they do not help the slow-transfer
  # case above, which is why the cache carries the load.
  sudo apt-get -o Acquire::Retries=3 update

  # Download and install as two steps, harvesting in between. Harvesting after a combined
  # install would depend on apt LEAVING the debs in place, which is not guaranteed — a
  # DPkg::Post-Invoke cleanup (Ubuntu's docker images ship exactly that) empties the archive
  # dir the moment dpkg runs, and the cache would silently stay empty forever. -d runs no
  # dpkg, so the debs are still there to harvest; the install that follows then needs 0 B.
  if [ -n "${APT_CACHE_DIR:-}" ]; then
    sudo apt-get -o Acquire::Retries=3 install -y --no-install-recommends -d "${DEPS[@]}"
    # Hand the debs back to the invoking user so actions/cache can read them. apt re-downloads
    # anything stale or corrupt, so a partial or outdated cache is slow at worst, never wrong.
    sudo cp -n /var/cache/apt/archives/*.deb "$APT_CACHE_DIR"/ 2>/dev/null || true
    sudo chown -R "$(id -u):$(id -g)" "$APT_CACHE_DIR" || true
    echo "==> apt cache: $(ls -1 "$APT_CACHE_DIR"/*.deb 2>/dev/null | wc -l) debs in $APT_CACHE_DIR"
  fi

  sudo apt-get -o Acquire::Retries=3 install -y --no-install-recommends "${DEPS[@]}"
fi

# [rc4l] Fail early with the exact package list rather than deep in a confusing CMake error.
for tool in cmake ninja git python3; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "ERROR: '$tool' not found. Install the build dependencies first:" >&2
    echo "       sudo apt-get install -y ${DEPS[*]}" >&2
    echo "       (or re-run with --install-deps)" >&2
    exit 1
  }
done

echo "==> Configuring (Release, OpenAL, NO_FMOD, SERVERONLY=$SERVERONLY)"
# [rc4l] Drop the cache but keep object files: a cache written before libopenal-dev was present
# keeps NO_OPENAL=OFF with no OPENAL_LIBRARY, silently producing a soundless binary.
rm -f build-linux/CMakeCache.txt

# [rc4l] ZX_WITH_SYMBOLS=1 (set by release CI) builds with debug info and splits it into
# zandronum.debug via RELEASE_WITH_DEBUG_FILE, keeping the shipped binary stripped/lean. The
# .debug file is uploaded to GlitchTip so crashes symbolicate. --build-id links the two.
SYM_ARGS=()
if [ "${ZX_WITH_SYMBOLS:-0}" = "1" ]; then
  echo "==> building with debug symbols (ZX_WITH_SYMBOLS=1)"
  SYM_ARGS=( -DRELEASE_WITH_DEBUG_FILE=ON
             -DCMAKE_CXX_FLAGS=-g -DCMAKE_C_FLAGS=-g
             -DCMAKE_EXE_LINKER_FLAGS=-Wl,--build-id )
fi

cmake -S src/zandronum -B build-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DSERVERONLY="$SERVERONLY" -DNO_FMOD=ON -DNO_GTK=ON -DFORCE_INTERNAL_JPEG=ON \
  "${SYM_ARGS[@]}"

echo "==> Building"
cmake --build build-linux -j"$(nproc)"

# [rc4l] Refuse to package a client that cannot make sound; this shipped once already.
if [ "$SERVERONLY" != "ON" ]; then
  if ! ldd build-linux/zandronum | grep -q libopenal; then
    echo "ERROR: zandronum is not linked against libopenal — the build has no sound." >&2
    echo "       Check the OpenAL detection in the configure output above." >&2
    exit 1
  fi
  echo "==> sound OK: $(ldd build-linux/zandronum | grep libopenal | tr -s ' ')"
fi

if [ "$NO_PACKAGE" = "ON" ]; then
  echo "==> Done (compile only; --no-package)"
  exit 0
fi

# --- Package ---------------------------------------------------------------
ARCH="$(uname -m)"
if [ -n "$VERSION" ]; then
  NAME="ZandroX-$VERSION-linux-$ARCH"
else
  NAME="ZandroX-linux-$ARCH"
fi
STAGE="dist-linux/$NAME"
rm -rf "$STAGE"; mkdir -p "$STAGE"

# [rc4l] SERVERONLY renames the target to zandronum-server; copying the hardcoded client name
# shipped whatever stale client binary happened to be in the build directory.
if [ "$SERVERONLY" = "ON" ]; then BIN=zandronum-server; else BIN=zandronum; fi
[ -f "build-linux/$BIN" ] || { echo "ERROR: build-linux/$BIN not found" >&2; exit 1; }
cp "build-linux/$BIN" "$STAGE"/
cp build-linux/*.pk3 "$STAGE"/
[ -f README.md ] && cp README.md "$STAGE"/ || true

# [rc4l] Ship Freedoom so the tarball is playable without a separate IWAD. It is
# BSD-3-clause, whose clause 2 requires the notice to accompany binary distributions.
if [ -f tools/freedoom/freedoom2.wad ]; then
  cp tools/freedoom/*.wad "$STAGE"/
  cp tools/freedoom/License.txt "$STAGE"/FREEDOOM-LICENSE.txt
fi

# [rc4l] GPL-3.0 sections 4-6: the binary must carry the license text and say where the
# corresponding source is, so these are required rather than best-effort.
cp LICENSE.txt "$STAGE"/
cp THIRD-PARTY-NOTICES.txt "$STAGE"/

tar czf "dist-linux/$NAME.tar.gz" -C dist-linux "$NAME"
echo "=== packaged: dist-linux/$NAME.tar.gz ==="
ls -la "dist-linux/$NAME.tar.gz"
echo "--- contents ---"
tar tzf "dist-linux/$NAME.tar.gz"
