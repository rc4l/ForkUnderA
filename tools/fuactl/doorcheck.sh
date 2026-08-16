#!/usr/bin/env bash
# [rc4l] Matched GL/Vulkan pair of a door caught MID-ANIMATION.
#
# A still level says nothing about moving geometry. This one bug -- the shut door staying painted
# across an open doorway -- was only visible with the sim frozen part-way through the raise, and
# reproducing it by hand took a dozen ordered calls, which is exactly how the first "fix" got
# declared working when it wasn't. Usage: bash doorcheck.sh [tag] [port]
set -euo pipefail

TAG="${1:-D1}"
PORT="${2:-7981}"
OUT="F:/ForkUnderA/dist-windows"
TASK=$(mktemp -u)

# [rc4l] Stock doom2 MAP01, not Sunder. Sunder is the PERFORMANCE map and has no use-activated doors
# at all -- door=1 matched zero lines there, and this test fell back to comparing two renders of a
# lift in a dark corridor, which agreed and proved nothing. MAP01 has the plainest door in the game.
#
# sv_nomonsters goes on the command line because it only takes effect at level start; setting it over
# the bridge afterwards leaves a room full of monsters shooting at whatever is being measured.
node src/cli.mjs launch --port "$PORT" --iwad doom2.wad --map "${MAP:-MAP01}" \
  --cvar sv_nomonsters=1 >"$TASK" 2>&1 &
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
R() { node src/cli.mjs rpc "$1" "$2" --port "$PORT" --token "$TOK" >/dev/null 2>&1; sleep "${3:-2}"; }

E "god" 1
# [rc4l] Where to stand and which way to look, so a bad guess is one env var away instead of an edit.
# The first run of this script pointed the camera down a corridor with no door in it and compared two
# renders of the same empty hallway -- which agreed perfectly and proved nothing.
E "fua_find_lines door=1 use=1 limit=8" 2
R player.setpos "{\"x\":${X:-992},\"y\":${Y:-1000},\"angle\":${ANG:-90}}" 2
E "gl_wallmesh 1" 3
E "fua_levelmesh_bakeall" 12
E "fua_diligent_scene" 12

# [rc4l] A BEFORE pair as well as an after one. Without it the test cannot distinguish "Vulkan
# tracked the motion" from "nothing moved and both renderers drew the same still wall" -- which is
# what the first lift run actually did.
node src/cli.mjs ui screenshot "${TAG}0_gl" --port "$PORT" --token "$TOK" >/dev/null 2>&1; sleep 2
E "fua_diligent_shot $OUT/${TAG}0_vk.png" 3
E "echo ===MESH-CLOSED" 0.5
E "fua_line_mesh ${LINE:-166}" 2

# Open it, then freeze part-way up. A door raises over ~64 tics; 20 lands it clearly in motion,
# with the old geometry gone and the new not yet at the ceiling.
E "+use" 0.3
E "-use" 0
R sim.step '{"tics":20}' 3
R sim.pause '{}' 2

node src/cli.mjs ui screenshot "${TAG}1_gl" --port "$PORT" --token "$TOK" >/dev/null 2>&1; sleep 2
E "fua_diligent_shot $OUT/${TAG}1_vk.png" 4
E "echo ===MESH-OPEN" 0.5
E "fua_line_mesh ${LINE:-166}" 2
# [rc4l] Everything standing at the doorway, not just what the door's own seg admits to owning.
E "fua_mesh_at ${X:-992} 1048 40" 3
# [rc4l] A third shot after an explicit full re-upload. This splits the two remaining explanations
# apart: if the door is open here but shut in shot 1, the piece list is fine and the per-frame refresh
# is not firing; if it is shut in both, the piece list itself still holds the closed door.
E "fua_diligent_scene" 10
E "fua_diligent_shot $OUT/${TAG}2_vk.png" 4
E "fua_dg_dynstats" 2

grep -nE "===MESH|^seg [0-9]+|^   piece|^piece |fua_mesh_at:|geometry:|uploaded" "$LOG" | tail -30
echo "TOKEN=$TOK PORT=$PORT LOG=$LOG"
