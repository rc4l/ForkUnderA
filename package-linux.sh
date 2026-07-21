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
# [rc4l] The container just runs linux_compile.sh — the same script a dev runs natively — so
# there is one build+package code path. The Ubuntu 22.04 image supplies its older glibc (wide
# distro compatibility) and the pre-installed deps, so no --install-deps is needed here.
docker run --rm -e SERVERONLY="$SERVERONLY" -e VERSION="${VERSION:-}" -v "$PWD:/work" "$IMAGE" \
  bash -lc './linux_compile.sh'
