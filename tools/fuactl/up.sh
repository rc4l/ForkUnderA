#!/usr/bin/env bash
# [rc4l] Start play.sh in the background and wait for it, BOUNDED and loudly.
#
# Written after a launch hung for six minutes with nothing on screen. The wait was
# `until [ -s .play-session ]; do sleep 0.4; done` -- which is correct only while play.sh is
# alive and on its way up. When it exits early instead (port still held by the last instance,
# a missing pk3, a bad map name) the session file never appears, the loop has no opinion about
# that, and it spins until something else times out. play.sh had already printed the reason and
# it went nowhere.
#
# So: poll for the file, but also watch whether the process is still breathing, and give up after
# a minute either way. A launch that is not going to work should say so in seconds.
#
#   MAP=dbab02 WAD=... SIDEBYSIDE=1 bash up.sh
#
# Every play.sh variable passes straight through -- this only owns the waiting.
set -uo pipefail

LOGF="${LOGF:-/tmp/play.out}"
DEADLINE="${DEADLINE:-60}"

# The previous instance holds the port, and its exit is not instant.
taskkill //F //IM forkundera.exe >/dev/null 2>&1 || true
rm -f .play-session

bash play.sh >"$LOGF" 2>&1 &
PID=$!

for _ in $(seq $((DEADLINE * 4))); do
  if [ -s .play-session ]; then
    cat .play-session
    grep -nE "uploaded|coverage:" "$LOGF" | tail -2
    exit 0
  fi
  if ! kill -0 "$PID" 2>/dev/null; then
    echo "play.sh exited before the session file appeared:" >&2
    tail -25 "$LOGF" >&2
    exit 1
  fi
  sleep 0.25
done

echo "no session file after ${DEADLINE}s -- play.sh is still running but has not come up:" >&2
tail -25 "$LOGF" >&2
exit 1
