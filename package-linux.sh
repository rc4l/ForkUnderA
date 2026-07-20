#!/usr/bin/env bash
# [rc4l] Reproducible Linux build + package of ZandroX via Docker.
#
# Runs the same on macOS (Apple Silicon -> aarch64 binary, runs natively under
# Docker Desktop) and on x86_64 Linux CI (-> x86_64 binary). Output:
#   dist-linux/ZandroX-linux-<arch>.tar.gz   (zandronum binary + game .pk3s)
#
#   ./package-linux.sh                    # full client
#   SERVERONLY=ON ./package-linux.sh      # headless server build
#   VERSION=v0.1.0 ./package-linux.sh     # stamp a version into the tarball name
#
# Audio is OpenAL (NO_FMOD): the binary links libopenal1 / libsndfile1 / libmpg123,
# which are standard on any desktop Linux (install them if a target box lacks them).
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

IMAGE=zandrox-linux
SERVERONLY="${SERVERONLY:-OFF}"

echo "==> Building Docker build image ($IMAGE)"
docker build -t "$IMAGE" -f Dockerfile.linux-build .

echo "==> Building + packaging ZandroX (SERVERONLY=$SERVERONLY) inside container"
docker run --rm -e SERVERONLY="$SERVERONLY" -e VERSION="${VERSION:-}" -v "$PWD:/work" "$IMAGE" bash -lc '
  set -euo pipefail
  # [rc4l] Drop the cache but keep the object files: a cache written before libopenal-dev was in
  # the image keeps NO_OPENAL=OFF with no OPENAL_LIBRARY, silently producing a soundless binary.
  rm -f build-linux/CMakeCache.txt
  cmake -S src/zandronum -B build-linux -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DSERVERONLY="${SERVERONLY:-OFF}" -DNO_FMOD=ON -DNO_GTK=ON -DFORCE_INTERNAL_JPEG=ON
  cmake --build build-linux -j"$(nproc)"

  # [rc4l] Refuse to package a client that cannot make sound; this shipped once already.
  if [ "${SERVERONLY:-OFF}" != "ON" ]; then
    if ! ldd build-linux/zandronum | grep -q libopenal; then
      echo "ERROR: zandronum is not linked against libopenal — the build has no sound." >&2
      echo "       Check the OpenAL detection in the configure output above." >&2
      exit 1
    fi
    echo "==> sound OK: $(ldd build-linux/zandronum | grep libopenal | tr -s " ")"
  fi

  ARCH="$(uname -m)"
  # [rc4l] Named here rather than renamed afterwards: on a Linux host the container writes
  # dist-linux as root, so the calling user cannot rename anything inside it.
  if [ -n "${VERSION:-}" ]; then
    NAME="ZandroX-$VERSION-linux-$ARCH"
  else
    NAME="ZandroX-linux-$ARCH"
  fi
  STAGE="dist-linux/$NAME"
  rm -rf "$STAGE"; mkdir -p "$STAGE"

  # The engine binary plus the game data pk3s it needs at runtime.
  cp build-linux/zandronum "$STAGE"/
  cp build-linux/*.pk3 "$STAGE"/
  [ -f README.md ] && cp README.md "$STAGE"/ || true

  # [rc4l] Ship Freedoom so the tarball is playable without a separate IWAD. It is
  # BSD-3-clause, whose clause 2 requires the notice to accompany binary distributions.
  if [ -f tools/freedoom/freedoom2.wad ]; then
    cp tools/freedoom/*.wad "$STAGE"/
    cp tools/freedoom/License.txt "$STAGE"/FREEDOOM-LICENSE.txt
  fi

  tar czf "dist-linux/$NAME.tar.gz" -C dist-linux "$NAME"
  echo "=== packaged: dist-linux/$NAME.tar.gz ==="
  ls -la "dist-linux/$NAME.tar.gz"
  echo "--- contents ---"
  tar tzf "dist-linux/$NAME.tar.gz"
'
