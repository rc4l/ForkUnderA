#!/usr/bin/env bash
# [rc4l] Capture a GL/Vulkan pair from an ALREADY RUNNING instance.
#
# Every check was relaunching the engine: ninety seconds of loading six pk3s, baking the level and
# uploading, for a two-second capture. The engine does not need restarting to answer "what does this
# look like now" -- only to pick up a new binary. This reuses whatever .play-session points at.
#
#   bash shot.sh <tag> [x y z angle [pitch]]
#   bash shot.sh <tag> --spot <name>          # a camera from spots.json
#
# With coordinates it warps first; without, it captures where the player is standing. A fault found
# by looking at it is worth a name, not four numbers pasted out of a screenshot -- spots.json holds
# the camera and what is supposed to be wrong there.
set -euo pipefail

TAG="${1:-shot}"
OUT=F:/ForkUnderA/dist-windows/sweep
[ -f .play-session ] || { echo "no .play-session -- nothing running"; exit 1; }
PORT=$(grep -oP 'PORT=\K[0-9]+' .play-session)
TOK=$(grep -oP 'TOKEN=\K\w+' .play-session)

X=""; Y=""; Z=""; ANG=""; PITCH=0
if [ "${2:-}" = "--spot" ]; then
  SPOT="${3:?usage: shot.sh <tag> --spot <name>}"
  read -r X Y Z ANG PITCH < <(node -e '
    const s = require("./spots.json").spots[process.argv[1]];
    if (!s) { console.error("no such spot: " + process.argv[1]); process.exit(1); }
    console.log([s.x, s.y, s.z, s.angle, s.pitch || 0].join(" "));
  ' "$SPOT")
  echo "spot $SPOT: $X $Y $Z angle $ANG pitch $PITCH"
elif [ $# -ge 5 ]; then
  X="$2"; Y="$3"; Z="$4"; ANG="$5"; PITCH="${6:-0}"
fi

if [ -n "$X" ]; then
  node src/cli.mjs rpc sim.resume '{}' --port "$PORT" --token "$TOK" >/dev/null 2>&1 || true
  sleep 1
  node src/cli.mjs rpc player.setpos \
    "{\"x\":$X,\"y\":$Y,\"z\":$Z,\"angle\":$ANG,\"pitch\":$PITCH}" \
    --port "$PORT" --token "$TOK" >/dev/null 2>&1
  sleep 2
fi

node src/cli.mjs rpc sim.pause '{}' --port "$PORT" --token "$TOK" >/dev/null 2>&1

# [rc4l] Wait for the FILE, not for a guess about how long the engine takes to write it. These were
# fixed sleeps totalling seven seconds a capture, which is most of a capture.
shot() {
  local want="$1"; shift
  rm -f "$want"
  node src/cli.mjs "$@" --port "$PORT" --token "$TOK" >/dev/null 2>&1
  for _ in $(seq 40); do [ -s "$want" ] && return 0; sleep 0.25; done
  echo "no $want after 10s" >&2; return 1
}
shot "$OUT/${TAG}_gl.png" ui screenshot "sweep/${TAG}_gl" || true
shot "$OUT/${TAG}_vk.png" ui exec "fua_diligent_shot $OUT/${TAG}_vk.png" || true
node pngstats.mjs --diff "$OUT/${TAG}_gl.png" "$OUT/${TAG}_vk.png"
node src/cli.mjs rpc sim.resume '{}' --port "$PORT" --token "$TOK" >/dev/null 2>&1 || true
