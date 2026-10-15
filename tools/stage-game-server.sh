#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Copies the server linux_compile.sh --serveronly staged in dist-linux/ to dist-prebuilt/, the one folder Dockerfile.game-server packages, so the pull request check and the published image stage it the same way.
#
# Usage: tools/stage-game-server.sh, from the repo root after ./linux_compile.sh --serveronly.
set -euo pipefail
shopt -s nullglob
staged=(dist-linux/ForkUnderA-*-server/)
[ "${#staged[@]}" -eq 1 ] || { echo "expected one staged server in dist-linux/, found ${#staged[@]}" >&2; exit 1; }
rm -rf dist-prebuilt
cp -r "${staged[0]}" dist-prebuilt
echo "staged ${staged[0]} as dist-prebuilt/"
