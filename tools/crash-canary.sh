#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Crash-reporting canary. Sends a well-formed test event to the baked-in Sentry/GlitchTip DSN
# and fails if the backend is unreachable or rejects it -- so CI (or a cron) catches a dead or
# misconfigured crash backend BEFORE real users silently lose reports (the v0.1.8 class of bug).
#
# Usage: tools/crash-canary.sh            (reads the DSN from src/zandronum/CMakeLists.txt)
#        ZX_SENTRY_DSN=<dsn> tools/crash-canary.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DSN="${ZX_SENTRY_DSN:-$(grep -oE 'https://[0-9a-fA-F-]+@[^"/]+/[0-9]+' "$ROOT/src/zandronum/CMakeLists.txt" | head -1)}"
[ -n "$DSN" ] || { echo "canary: could not find a DSN"; exit 2; }

# Parse https://KEY@HOST/PROJECT
rest="${DSN#https://}"
KEY="${rest%%@*}"
hostproj="${rest#*@}"
HOST="${hostproj%%/*}"
PROJ="${hostproj##*/}"
echo "canary: host=$HOST project=$PROJ"

EID="$(python3 -c 'import uuid;print(uuid.uuid4().hex)' 2>/dev/null || openssl rand -hex 16)"
resp="$(mktemp)"
code="$(curl -sS -m 25 -o "$resp" -w '%{http_code}' -X POST "https://$HOST/api/$PROJ/store/" \
	-H 'Content-Type: application/json' \
	-H "X-Sentry-Auth: Sentry sentry_version=7, sentry_key=$KEY, sentry_client=zandrox-canary/1.0" \
	-d "{\"event_id\":\"$EID\",\"message\":\"ZandroX crash-canary\",\"level\":\"info\",\"platform\":\"native\"}" || true)"

echo "canary: HTTP $code"
cat "$resp" 2>/dev/null; echo
rm -f "$resp"

if [ "$code" != "200" ]; then
	echo "canary: FAILED -- the crash backend did not accept a well-formed event." >&2
	exit 1
fi
echo "canary: OK -- GlitchTip accepted event $EID"
