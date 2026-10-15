#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Proves a game server image HOSTS, not merely that it built: three servers from it (a catalogue entry, one naming a file, a server folder), each checked for what a working server prints.
# [rc4l] Every entry is synthesised from what the image carries (freedoom2.wad, its own pk3s), so the test needs no third-party mirror, and announce is off so no CI server reaches the public browser.
#
# Usage: tools/game-server-smoke.sh <image>; run from the repo root, it writes smoke/ and *.log beside it.
set -euo pipefail
img="${1:?usage: game-server-smoke.sh <image>}"

mkdir -p smoke/catalogue/citest
cat > smoke/catalogue/citest/addon.json <<'JSON'
{
  "schema": 1,
  "name": "CI smoke test",
  "summary": "Resolves entirely from inside the image, so this test needs no network.",
  "iwad": "freedoom2.wad",
  "map": "map01",
  "files": []
}
JSON

# [rc4l] A second entry names a file, whose md5 is read from the image, since an empty files[] never called resolve_file and a -file full of log lines once shipped behind a healthy-looking server.
pk3="$(docker run --rm --entrypoint sh "$img" -c 'ls /opt/forkundera/brightmaps.pk3')"
md5="$(docker run --rm --entrypoint sh "$img" -c "md5sum $pk3 | cut -d' ' -f1")"
echo "smoke file: $pk3 md5=$md5"

# [rc4l] The file lives in a variant rather than the entry's shared list, since the entrypoint once read only the shared list and hosted Duel 40 with none of Duel 40 in it.
mkdir -p smoke/catalogue/citestfile
cat > smoke/catalogue/citestfile/addon.json <<JSON
{
  "schema": 1,
  "name": "CI smoke test with a file",
  "iwad": "freedoom2.wad",
  "map": "map01",
  "files": [],
  "variants": [
    { "id": "only", "name": "the only way to play it", "cfg": "server.cfg", "default": true,
      "files": [ { "name": "$(basename "$pk3")", "md5": "${md5}" } ] }
  ]
}
JSON

# [rc4l] The container's unprivileged user does not exist on the runner, so the bind mount it writes its WAD store into is opened up; a real deployment's named volume needs nothing.
chmod -R 777 smoke

docker run -d --name fua-smoke \
  -e FUA_ANNOUNCE=0 \
  -e FUA_NAME="CI smoke test" \
  -v "$PWD/smoke:/data" \
  "$img" citest
sleep 20

# [rc4l] Logs go to a file and are grepped there, since `grep -q` in a pipe under pipefail kills `docker logs` with SIGPIPE and fails exactly when the line is found.
docker logs fua-smoke > smoke.log 2>&1
cat smoke.log

if [ "$(docker inspect -f '{{.State.Running}}' fua-smoke)" != "true" ]; then
  echo "::error::the server exited instead of staying up"
  exit 1
fi

# [rc4l] The entrypoint got as far as handing off to the engine.
grep -q '\[fua-entry\] exec:' smoke.log \
  || { echo "::error::entrypoint never built a command line"; exit 1; }

# [rc4l] The engine's own banner, since "freedoom" is printed by the entrypoint too and passed against a binary that never ran.
grep -q 'ForkUnderA' smoke.log \
  || { echo "::error::engine never printed its banner -- the binary did not start"; exit 1; }

# [rc4l] Serving rather than merely alive: a bound socket, a loaded map, and the TCP listener features/wad-serve opens beside it.
grep -q 'UDP Initialized' smoke.log \
  || { echo "::error::network subsystem never came up"; exit 1; }
grep -q '\*\*\* map01' smoke.log \
  || { echo "::error::no map loaded"; exit 1; }
grep -q 'WAD serving enabled on TCP port' smoke.log \
  || { echo "::error::wad-serve did not open its TCP listener"; exit 1; }

# [rc4l] Config creation comes after the banner and kills the process when it fails, which is how the HOME-on-the-volume bug presented.
if grep -q 'Permission denied' smoke.log; then
  echo "::error::server hit a permission error during startup"
  exit 1
fi

docker rm -f fua-smoke

# [rc4l] The second server, whose last assertion matters most: the engine reporting it added the pk3.
docker run -d --name fua-smoke-file \
  -e FUA_ANNOUNCE=0 \
  -v "$PWD/smoke:/data" \
  "$img" citestfile
sleep 20
docker logs fua-smoke-file > smokefile.log 2>&1
cat smokefile.log

if [ "$(docker inspect -f '{{.State.Running}}' fua-smoke-file)" != "true" ]; then
  echo "::error::the file-entry server exited instead of staying up"
  exit 1
fi

grep -qE '^ adding .*brightmaps\.pk3' smokefile.log \
  || { echo "::error::the engine never loaded the entry's file -- resolve_file returned something unusable"; exit 1; }

# [rc4l] The path handed over is one clean argument, where a -file carrying log output would not be a plain path.
grep -q "\-file /opt/forkundera/brightmaps.pk3" smokefile.log \
  || grep -q "\-file /data/wads" smokefile.log \
  || { echo "::error::the -file argument was not a clean resolved path"; exit 1; }

# [rc4l] server.cfg wins: with no FUA_NAME or FUA_PLAYERS set, the entrypoint passes neither cvar, since `+` arguments come after `+exec` and would overrule the cfg.
if grep -qE '\[fua-entry\] exec:.*\+sv_maxplayers' smokefile.log; then
  echo "::error::+sv_maxplayers was passed with no FUA_PLAYERS set -- it would override server.cfg"
  exit 1
fi
if grep -qE '\[fua-entry\] exec:.*\+sv_hostname' smokefile.log; then
  echo "::error::+sv_hostname was passed with no FUA_NAME set -- it would override server.cfg"
  exit 1
fi

# [rc4l] And an explicit override still reaches the command line, which the first server's FUA_NAME shows.
grep -qE '\[fua-entry\] exec:.*\+sv_hostname' smoke.log \
  || { echo "::error::FUA_NAME was set but +sv_hostname never reached the command line"; exit 1; }

docker rm -f fua-smoke-file

# [rc4l] Folder mode: a server folder with a cfg and a wads.txt and no catalogue, from files the image carries.
mkdir -p smoke/servers/folderci
: > smoke/servers/folderci/server.cfg
cat > smoke/servers/folderci/wads.txt <<TXT
# comments and blank lines must be ignored

iwad freedoom2.wad
file $(basename "$pk3")
TXT
chmod -R 777 smoke

docker run -d --name fua-smoke-folder \
  -e FUA_ANNOUNCE=0 \
  -e FUA_SERVER_DIR=/data/servers/folderci \
  -v "$PWD/smoke:/data" \
  "$img"
sleep 20
docker logs fua-smoke-folder > smokefolder.log 2>&1
cat smokefolder.log

if [ "$(docker inspect -f '{{.State.Running}}' fua-smoke-folder)" != "true" ]; then
  echo "::error::the folder-mode server exited instead of staying up"
  exit 1
fi
grep -qE '^ adding .*brightmaps\.pk3' smokefolder.log \
  || { echo "::error::folder mode did not load the wads.txt file"; exit 1; }
grep -q 'UDP Initialized' smokefolder.log \
  || { echo "::error::folder-mode server never bound its socket"; exit 1; }

docker rm -f fua-smoke-folder
