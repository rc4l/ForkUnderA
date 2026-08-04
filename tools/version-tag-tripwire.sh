#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Version-tag tripwire: fails if `git describe` would name the game build after something
# other than a game release tag.
#
# Born from a real break. The repo carries two INDEPENDENT tag namespaces: `v*` for game releases and
# `server-registry-v*` for the container image, which ships on its own schedule. updaterevision ran a
# bare `git describe --tags`, which takes the nearest tag of ANY name -- so the moment a
# server-registry image was tagged, every subsequent game build called itself
# "server-registry-v0.0.2". That string went into the window title, the crash-report footer and the
# update check, where it parses as version 0.0.0 and makes every published release look newer, so the
# update notice would never switch off.
#
# Nothing failed. It built clean, the tests passed, and the only symptom was a wrong name in a title
# bar -- which is exactly why it needs a tripwire rather than a comment. Any new tag namespace
# (nightly-*, tools-*, whatever comes next) reintroduces it the same silent way.
#
# The rule: updaterevision must constrain describe to the game namespace, and the value it actually
# bakes must look like a game release.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

fail=0
SRC="src/zandronum/tools/updaterevision/updaterevision.c"

# 1. The query must say which namespace it means.
if ! grep -q -- '--match "v\*"' "$SRC"; then
	echo "$SRC: 'git describe' is not restricted to game release tags (--match \"v*\")." >&2
	echo "  Without it the newest tag of ANY namespace (e.g. server-registry-v*) names the build." >&2
	fail=1
fi

# 2. And the value it produces must actually be a game release. Skipped when the checkout has no
#    v* tags at all -- a shallow CI clone or a fresh fork is not a failure, it just has nothing to
#    check, and --always makes describe fall back to a bare hash there.
if git rev-parse --git-dir >/dev/null 2>&1; then
	if git tag --list 'v*' | grep -q .; then
		described="$(git describe --tags --long --always --match 'v*' 2>/dev/null || true)"
		case "$described" in
			v[0-9]*) ;;
			"")
				echo "warning: 'git describe --match v*' produced nothing; skipping value check." >&2
				;;
			*)
				echo "git describe resolved to '$described', which is not a v<number> game release." >&2
				fail=1
				;;
		esac
	fi
fi

if [ "$fail" -ne 0 ]; then
	echo "version-tag tripwire FAILED." >&2
	exit 1
fi

echo "version-tag tripwire: game version comes from a v* release tag."
