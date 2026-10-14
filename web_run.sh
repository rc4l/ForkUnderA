#!/usr/bin/env bash
# [rc4l] Build the browser version if anything changed, serve it from localhost and open it: web_compile.sh's counterpart of mac_build_run.sh.
#
#   EMSDK=/path/to/emsdk ./web_run.sh [--no-build] [--no-open] [--port N] [query]
#
# The query goes on the page's address as it is, such as map=MAP01 or iwad=doom2.wad&warp=1%201. Ctrl+C stops the server.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

BUILD=1
OPEN=1
PORT=8080
QUERY=""
while [ $# -gt 0 ]; do
  case "$1" in
    --no-build) BUILD=0 ;;
    --no-open) OPEN=0 ;;
    --port) PORT="$2"; shift ;;
    *) QUERY="$1" ;;
  esac
  shift
done

if [ "$BUILD" = 1 ]; then ./web_compile.sh; fi
[ -f dist-web/index.html ] || { echo "dist-web/ has no page yet: run ./web_run.sh without --no-build once" >&2; exit 1; }

URL="http://localhost:$PORT/${QUERY:+?$QUERY}"
open_browser() {
  case "$(uname -s)" in
    Darwin) open "$1" ;;
    # [rc4l] explorer rather than cmd's start, which would split the address at its &.
    MINGW*|MSYS*|CYGWIN*) explorer.exe "$1" || true ;;
    *) xdg-open "$1" >/dev/null 2>&1 || echo "open $1 in Chrome or Edge" ;;
  esac
}

# [rc4l] The browser opens once the server answers.
if [ "$OPEN" = 1 ]; then
  (
    for _ in $(seq 100); do
      if node -e "fetch(process.argv[1]).then(r => process.exit(r.ok ? 0 : 1), () => process.exit(1))" "$URL"; then open_browser "$URL"; exit 0; fi
      sleep 0.1
    done
    echo "the server did not answer; open $URL yourself" >&2
  ) &
fi
echo "serving dist-web/ at $URL"
exec node tools/web/serve.mjs dist-web "$PORT"
