#!/usr/bin/env bash
# [rc4l] What is the surface under the crosshair, and what does the mesh hold for it?
#
#   bash look.sh              # wherever the player is standing right now
#   bash look.sh x y z angle [pitch]
#
# The loop this replaces: screenshot -> guess a cause -> rebuild -> ask someone to walk back there
# and look again. Four rounds of that on one pane of glass. The question every round was really
# asking is "what does the mesh say about THIS surface", and now that is one command against the
# running instance -- no rebuild, no screenshot, no walking.
#
# Prints the engine's own trace alongside the mesh's answer, because "the engine has a linedef here
# and the mesh has nothing" and "both agree and the blend mode is wrong" are different bugs that look
# identical from the outside.
set -uo pipefail

[ -f .play-session ] || { echo "no .play-session -- nothing running"; exit 1; }
PORT=$(grep -oP 'PORT=\K[0-9]+' .play-session)
TOK=$(grep -oP 'TOKEN=\K\w+' .play-session)

if [ $# -ge 4 ]; then
  node src/cli.mjs rpc player.setpos \
    "{\"x\":$1,\"y\":$2,\"z\":$3,\"angle\":$4,\"pitch\":${5:-0}}" \
    --port "$PORT" --token "$TOK" >/dev/null 2>&1
  sleep 1
fi

node src/cli.mjs ui exec "fua_look" --port "$PORT" --token "$TOK" >/dev/null 2>&1

LOG=$(ls -t /c/Users/anann/AppData/Local/Temp/fuactl-*/engine-"$PORT".log | head -1)
for _ in $(seq 20); do
  grep -q "fua_look:" "$LOG" && break
  sleep 0.2
done
# The last block only: this log accumulates every previous look.
awk '/fua_look:/ { buf = "" } { buf = buf $0 "\n" } END { printf "%s", buf }' "$LOG" | tail -12
