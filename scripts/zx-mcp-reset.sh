#!/usr/bin/env bash
# zx-mcp-reset — reap ZandroX/Zandronum engine processes left behind by the MCP.
#
# With the v0.4.0 engine watchdog, a launched instance exits on its own when its
# MCP session dies, so orphans should be rare. This is the manual recovery hatch:
# it force-kills every running engine and reports any that are wedged in the OS
# (macOS window-server exit-hang) and survive SIGKILL — those need Activity
# Monitor or a logout to clear.
#
#   ./scripts/zx-mcp-reset.sh
set -u

# Match the engine binaries (bundled app, loose build binary, and the MCP's
# zandronum-mcp-hooks), excluding the MCP node server and this script itself.
# Keep only real engine binaries: a path component `/zandronum` or the bridge
# build `zandronum-mcp-hooks`. Excludes the MCP launcher (`npm exec zandronum-mcp`,
# `node …`) and this script.
pids=$(ps -A -o pid=,command= \
  | grep -iE '/zandronum(-mcp-hooks)?( |$)|/zandronum ' \
  | grep -viE 'node|npm|zx-mcp-reset|grep' \
  | awk '{print $1}')

if [ -z "${pids:-}" ]; then
  echo "No engine processes running. Nothing to reset."
  exit 0
fi

echo "Killing engine PIDs: $(echo "$pids" | tr '\n' ' ')"
# shellcheck disable=SC2086
kill -9 $pids 2>/dev/null
sleep 1

survivors=""
for p in $pids; do
  if kill -0 "$p" 2>/dev/null; then survivors="$survivors $p"; fi
done

if [ -n "$survivors" ]; then
  echo "WEDGED (survived SIGKILL):$survivors"
  echo "These are stuck in the OS (macOS window-server exit-hang). Force-quit them in"
  echo "Activity Monitor, or log out/in to clear. Their state shows as UE/TX in \`ps\`."
  exit 1
fi

echo "All engine processes reaped cleanly."
