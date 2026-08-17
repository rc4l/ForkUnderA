#!/usr/bin/env bash
# [rc4l] A build you can actually walk around in, with the Vulkan view live beside the game window.
#
# Every other script here is hands-off: the bridge drops OS keyboard and mouse so a stray cursor
# cannot diverge a run from its twin. That is right for measuring and exactly wrong when the point is
# for a human to go find something and point at it. This one hands the input back.
#
# One window. The backend renders into a disabled child covering the engine's client area, so the
# pixels you see are Vulkan while the mouse and keyboard go to the engine exactly as they always did.
# GL still draws underneath, unseen -- that is what keeps the wall cache fed as you walk.
#
#   bash play.sh                 # doom2 MAP01
#   MAP=MAP07 bash play.sh
#   WAD=/path/to/x.wad bash play.sh
#
# It prints a port and token and then stays up, so `fuactl ui screenshot` / `fua_diligent_shot` can
# capture whatever you have walked to without restarting anything.
set -euo pipefail

PORT="${PORT:-7900}"
MAP="${MAP:-MAP01}"
IWAD="${IWAD:-doom2.wad}"
OUT="F:/ForkUnderA/dist-windows"
# [rc4l] The port and token land in a fixed file, not just on stdout.
#
# stdout is whatever the caller piped it into, and a play session that has to be found again -- to
# take a Vulkan screenshot of the thing you just walked to -- must not depend on that pipe still
# existing. This is what every helper below reads.
SESSION="${SESSION:-$(pwd)/.play-session}"
TASK=$(mktemp -u)

# [rc4l] fua_vulkan does the bake, the upload and the going-live by itself, per level. It used to be
# three console commands in a fixed order from down here, which worked exactly once: change map and
# the mesh was still the old one's, so the new level rendered as the previous level's geometry.
# GL=1 launches the same build on the GL renderer for an A/B.
# [rc4l] PRESET/VARIANT resolve a catalogue entry to its file list, so playing what the HOST tab
# offers does not mean copying six pk3 paths out of a JSON file by hand.
#
#   PRESET=gvh VARIANT=lod MAP=gvh04 bash play.sh
#
# Files come from the same download store the catalogue itself uses, so anything already fetched
# through the HOST tab just works. A missing file is named rather than silently dropped: a wad list
# that is quietly one short loads a map with no textures and looks like a renderer bug.
if [ -n "${PRESET:-}" ]; then
  WAD=$(node -e '
    const fs=require("fs"),path=require("path");
    const id=process.argv[1], want=process.argv[2];
    const d=JSON.parse(fs.readFileSync(path.resolve("../../catalogue",id,"addon.json"),"utf8"));
    const v=want ? d.variants.find(x=>x.id===want) : (d.variants.find(x=>x.default)||d.variants[0]);
    if(!v){console.error("no such variant: "+want);process.exit(1);}
    const dir=process.env.LOCALAPPDATA+"/ForkUnderA/pwads";
    const out=[],missing=[];
    for(const f of (v.files||[])){const p=path.join(dir,f.name);(fs.existsSync(p)?out:missing).push(p);}
    if(missing.length){console.error("missing, fetch with fua_download: "+missing.join(" "));process.exit(1);}
    process.stdout.write(out.join(","));
  ' "$PRESET" "${VARIANT:-}") || exit 1
fi

ARGS=(launch --port "$PORT" --iwad "$IWAD" --map "$MAP" --play)
[ -n "${GL:-}" ] || ARGS+=(--cvar fua_vulkan=1)
# [rc4l] SIDEBYSIDE=1 gives the backend its own window next to the engine's instead of embedding it,
# so both renderers are on screen at once. Easier to point at a difference than toggling and trying
# to remember what the other one looked like.
[ -n "${SIDEBYSIDE:-}" ] && ARGS+=(--cvar fua_dg_embed=0)
[ -n "${WAD:-}" ] && ARGS+=(--file "$WAD")
# God on and monsters off by default: being shot at halfway through showing me a texture seam is
# pure friction. MONSTERS=1 puts them back when the thing to look at IS a monster sprite.
[ -n "${MONSTERS:-}" ] || ARGS+=(--cvar sv_nomonsters=1)

node src/cli.mjs "${ARGS[@]}" >"$TASK" 2>&1 &
LAUNCH=$!
trap 'kill $LAUNCH 2>/dev/null || true' EXIT

for _ in $(seq 60); do grep -q 'token=' "$TASK" 2>/dev/null && break; sleep 2; done
TOK=$(grep -o 'token=[a-f0-9]*' "$TASK" | cut -d= -f2)
[ -n "$TOK" ] || { echo "launch failed:"; tail -20 "$TASK"; exit 1; }
for _ in $(seq 40); do
  node src/cli.mjs rpc sim.tic --port "$PORT" --token "$TOK" 2>/dev/null | grep -q '"inlevel": true' && break
  sleep 3
done

printf 'PORT=%s
TOKEN=%s
' "$PORT" "$TOK" > "$SESSION"

E() { node src/cli.mjs ui exec "$1" --port "$PORT" --token "$TOK" >/dev/null 2>&1; sleep "${2:-2}"; }

# [rc4l] freelook is OFF in a stock Doom config, and every bridge instance gets a fresh one, so a
# play session came up unable to look up or down at all. Not the backend's fault, but indistinguishable
# from it at the window.
E "freelook 1" 1
E "god" 1

LOG=$(ls -t /c/Users/anann/AppData/Local/Temp/fuactl-*/engine-"$PORT".log | head -1)
grep -nE "uploaded|coverage:" "$LOG" | tail -3
cat <<EOF

  ready -- the window is showing the VULKAN render. mouse and keyboard work normally.
  walking into unbaked areas bakes them in as you go.
  fua_vulkan 0 / 1 in the console is a live A/B against GL. changing map re-bakes by itself.

  PORT=$PORT TOKEN=$TOK   (also written to $SESSION)
  vulkan shot:  node src/cli.mjs ui exec "fua_diligent_shot $OUT/look.png" --port $PORT --token $TOK
  gl shot:      node src/cli.mjs ui screenshot look_gl --port $PORT --token $TOK
  A/B:          node src/cli.mjs ui exec "fua_vulkan 0" --port $PORT --token $TOK

  ctrl-C here to quit.
EOF
wait $LAUNCH
