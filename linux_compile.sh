#!/usr/bin/env bash
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
# OpenSSL/Opus/zlib/bzip2/jpeg + Python3, SDL1.2 + OpenGL/GLU/GLEW + GME for the client,
# and the OpenAL stack (libopenal + libsndfile + libmpg123) that replaces FMOD.
DEPS=(
  build-essential cmake ninja-build git python3 pkg-config ca-certificates
  libssl-dev libopus-dev zlib1g-dev libbz2-dev libjpeg-dev
  libsdl1.2-dev libgl1-mesa-dev libglu1-mesa-dev libglew-dev libgme-dev
  libopenal-dev libsndfile1-dev libmpg123-dev
)

if [ "$INSTALL_DEPS" = "ON" ]; then
  echo "==> Installing build dependencies (apt)"
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends "${DEPS[@]}"
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
cmake -S src/zandronum -B build-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DSERVERONLY="$SERVERONLY" -DNO_FMOD=ON -DNO_GTK=ON -DFORCE_INTERNAL_JPEG=ON

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
