#!/usr/bin/env bash
#
# Regenerate a ledger from its upstream clone: `tracker.py regen`, kept as the familiar entry point.
#
# Writes coverage.tsv (one row per commit, in ledger order), index.tsv (path -> commits), files.tsv
# (commit -> paths) and progress.json in the ledger's folder. Re-runnable: status, note, ours and
# twin are preserved per sha, new commits are born `pending`, and curated rows whose commit left
# upstream history move to orphans.tsv instead of vanishing.
#
# Usage:  ./regen.sh [--ledger uzdoom|zandronum] [--to REV]
#         clones default to ../UZDoom and ../Zandronum beside the checkout (ledgers.json);
#         UZDOOM=/path and ZANDRONUM=/path override them.
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$DIR/python.sh"
exec "${PY[@]}" "$DIR/tracker.py" regen "$@"
