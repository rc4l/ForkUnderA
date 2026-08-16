#!/usr/bin/env bash
# [rc4l] One command: launch, full-level bake, upload, and capture a matched GL/Vulkan pair.
#
# The comparison that matters is the same camera in both renderers, and doing it by hand meant a
# dozen `ui exec` calls in the right order every time -- which is how a stale binary and a
# wrong-camera comparison both slipped through earlier. Usage:
#   bash vkcheck.sh [port] [outdir]
set -euo pipefail

PORT="${1:-7976}"
OUT="${2:-F:/ForkUnderA/dist-windows}"
WAD="C:/Users/anann/AppData/Local/ForkUnderA/pwads/sunder_2512.wad"
TASK=$(mktemp -u)

node src/cli.mjs launch --port "$PORT" --iwad doom2.wad --file "$WAD" --map MAP10 >"$TASK" 2>&1 &
LAUNCH=$!
trap 'kill $LAUNCH 2>/dev/null || true' EXIT

for _ in $(seq 60); do grep -q 'token=' "$TASK" 2>/dev/null && break; sleep 3; done
TOK=$(grep -o 'token=[a-f0-9]*' "$TASK" | cut -d= -f2)
[ -n "$TOK" ] || { echo "launch failed:"; tail -20 "$TASK"; exit 1; }

for _ in $(seq 40); do
  node src/cli.mjs rpc sim.tic --port "$PORT" --token "$TOK" 2>/dev/null | grep -q '"inlevel": true' && break
  sleep 4
done

LOG=$(ls -t /c/Users/anann/AppData/Local/Temp/fuactl-*/engine-"$PORT".log | head -1)
E() { node src/cli.mjs ui exec "$1" --port "$PORT" --token "$TOK" >/dev/null 2>&1; sleep "${2:-2}"; }

E "gl_wallmesh 1" 4
E "fua_levelmesh_bakeall" 12
E "fua_diligent_scene" 12
# GL first, then Vulkan, with no camera change in between.
node src/cli.mjs ui screenshot glref --port "$PORT" --token "$TOK" >/dev/null 2>&1; sleep 2
E "fua_diligent_shot $OUT/vkref.png" 4
E "fua_levelmesh_stats" 2

# [rc4l] Benchmarks run inside this script, not after it: the exit trap kills the instance, so a
# follow-up `fuactl diligent` against the same port silently measured nothing.
if [ "${BENCH:-0}" = "1" ]; then
  E "fua_diligent_bench 300" 12
  E "fua_gl_meshbench 300" 6
  E "fua_diligent_scale 60" 30
fi

grep -nE "mesh light:|uploaded|coverage:|captures:|Diligent:|GL mesh|=> ~|[0-9]x = " "$LOG" | tail -16
echo "TOKEN=$TOK PORT=$PORT LOG=$LOG"
