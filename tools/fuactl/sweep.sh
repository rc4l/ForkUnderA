#!/usr/bin/env bash
# [rc4l] Matched GL/Vulkan pairs across several maps, ranked by how much they disagree.
#
# Deciding what to port next off a feature list is guessing: the list says portals, 3D floors, decals,
# models, and says nothing about which of them a player actually walks past. This walks a spread of
# stock maps, captures the same camera in both renderers, and prints a number per map, so the next
# thing to fix is the worst number rather than the most interesting-sounding entry.
#
# It also exercises the per-level re-bake, which is the one path a single-map test can never reach.
#
#   bash sweep.sh                    # doom2 MAP01 02 07 11 15 27
#   MAPS="MAP01 MAP31" bash sweep.sh
set -euo pipefail

PORT="${PORT:-7902}"
MAPS="${MAPS:-MAP01 MAP02 MAP07 MAP11 MAP15 MAP27}"
OUT="F:/ForkUnderA/dist-windows/sweep"
TASK=$(mktemp -u)
mkdir -p "$OUT"

node src/cli.mjs launch --port "$PORT" --iwad doom2.wad --map MAP01 \
  --cvar sv_nomonsters=1,fua_vulkan=1 >"$TASK" 2>&1 &
LAUNCH=$!
trap 'kill $LAUNCH 2>/dev/null || true' EXIT

for _ in $(seq 60); do grep -q 'token=' "$TASK" 2>/dev/null && break; sleep 2; done
TOK=$(grep -o 'token=[a-f0-9]*' "$TASK" | cut -d= -f2)
[ -n "$TOK" ] || { echo "launch failed:"; tail -20 "$TASK"; exit 1; }
for _ in $(seq 40); do
  node src/cli.mjs rpc sim.tic --port "$PORT" --token "$TOK" 2>/dev/null | grep -q '"inlevel": true' && break
  sleep 3
done

E() { node src/cli.mjs ui exec "$1" --port "$PORT" --token "$TOK" >/dev/null 2>&1; sleep "${2:-2}"; }
R() { node src/cli.mjs rpc "$1" "$2" --port "$PORT" --token "$TOK" >/dev/null 2>&1; sleep "${3:-2}"; }

LOG=$(ls -t /c/Users/anann/AppData/Local/Temp/fuactl-*/engine-"$PORT".log | head -1)

E "god" 1
for M in $MAPS; do
  # [rc4l] Wait for THIS map's upload, do not guess at it with a sleep.
  #
  # A fixed sleep captured the frame before the new mesh landed, so the backend was drawing the
  # PREVIOUS map while GL drew this one -- and the pair scored as a catastrophic rendering bug.
  # MAP15 came out at mean|d| 41.6 that way and 8.9 once the wait was real, which is the difference
  # between "the sky is broken" and "the sky is fine". Counting the upload lines is exact.
  BEFORE=$(grep -c "^vulkan: uploaded" "$LOG" || true)
  E "map $M" 3
  for _ in $(seq 40); do
    NOW=$(grep -c "^vulkan: uploaded" "$LOG" || true)
    [ "$NOW" -gt "$BEFORE" ] && break
    sleep 2
  done
  sleep 3
  R sim.pause '{}' 2
  node src/cli.mjs ui screenshot "sweep/${M}_gl" --port "$PORT" --token "$TOK" >/dev/null 2>&1; sleep 2
  E "fua_diligent_shot $OUT/${M}_vk.png" 3
  R sim.resume '{}' 1
done

echo
echo "=== GL vs Vulkan, per map (higher mean|d| = worse) ==="
ARGS=()
for M in $MAPS; do
  [ -f "$OUT/${M}_gl.png" ] && [ -f "$OUT/${M}_vk.png" ] && ARGS+=("$OUT/${M}_gl.png" "$OUT/${M}_vk.png")
done
[ ${#ARGS[@]} -gt 0 ] && node pngstats.mjs --diff "${ARGS[@]}"

echo; grep -nE "^vulkan:" "$LOG" | tail -8
echo "TOKEN=$TOK PORT=$PORT LOG=$LOG"
