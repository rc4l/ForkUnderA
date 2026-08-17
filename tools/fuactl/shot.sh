#!/usr/bin/env bash
# [rc4l] Capture a GL/Vulkan pair from an ALREADY RUNNING instance.
#
# Every check was relaunching the engine: ninety seconds of loading six pk3s, baking the level and
# uploading, for a two-second capture. The engine does not need restarting to answer "what does this
# look like now" -- only to pick up a new binary. This reuses whatever .play-session points at.
#
#   bash shot.sh <tag> [x y z angle]
#
# With coordinates it warps first; without, it captures where the player is standing.
set -euo pipefail

TAG="${1:-shot}"
OUT=F:/ForkUnderA/dist-windows/sweep
[ -f .play-session ] || { echo "no .play-session -- nothing running"; exit 1; }
PORT=$(grep -oP 'PORT=\K[0-9]+' .play-session)
TOK=$(grep -oP 'TOKEN=\K\w+' .play-session)

if [ $# -ge 5 ]; then
  node src/cli.mjs rpc sim.resume '{}' --port "$PORT" --token "$TOK" >/dev/null 2>&1 || true
  sleep 1
  node src/cli.mjs rpc player.setpos "{\"x\":$2,\"y\":$3,\"z\":$4,\"angle\":$5}" \
    --port "$PORT" --token "$TOK" >/dev/null 2>&1
  sleep 2
fi

node src/cli.mjs rpc sim.pause '{}' --port "$PORT" --token "$TOK" >/dev/null 2>&1
sleep 1
node src/cli.mjs ui screenshot "sweep/${TAG}_gl" --port "$PORT" --token "$TOK" >/dev/null 2>&1
sleep 1
node src/cli.mjs ui exec "fua_diligent_shot $OUT/${TAG}_vk.png" --port "$PORT" --token "$TOK" >/dev/null 2>&1
sleep 2
node pngstats.mjs --diff "$OUT/${TAG}_gl.png" "$OUT/${TAG}_vk.png"
node src/cli.mjs rpc sim.resume '{}' --port "$PORT" --token "$TOK" >/dev/null 2>&1 || true
