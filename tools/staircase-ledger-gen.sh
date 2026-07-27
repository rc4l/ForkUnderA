#!/usr/bin/env bash
# staircase-ledger-gen.sh <from-sha> <to-sha> [uzdoom-clone]
#
# Emits base ledger rows (TSV) for every commit in (from, to] that touches the GL
# renderer or its shaders. Columns: sha date category status zandrox_sha note.
# All rows come out status=pending, zandrox_sha=—, note=<upstream subject>; a flight
# then edits the rows it resolves. Category is a keyword heuristic over subject+paths.
#
# Prints rows to stdout (no header) so it can seed a new range or diff against the file.
set -u
FROM="${1:?from-sha}"; TO="${2:?to-sha}"
CLONE="${3:-/Users/talhataj/repos/UZDoom}"
cd "$CLONE" || { echo "no clone at $CLONE" >&2; exit 1; }

categorize() {
	# $1 = subject (lowercased), $2 = newline-joined paths
	local s="$1" p="$2"
	case "$s $p" in
		*shader*|*glsl*|*.fp*|*.vp*)          echo shader ;;
		*sampler*|*hwtexture*|*gl_material*|*texture\ *|*gl_texture*) echo texture ;;
		*brightmap*|*light\ buffer*|*lightbuffer*|*dynlight*|*dynamic\ light*) echo dynlights ;;
		*lightmode*|*colormap*|*fog*|*gl_lightdata*) echo lighting ;;
		*model*|*md2*|*md3*|*voxel*|*dmd*)    echo models ;;
		*sky*|*skydome*|*skybox*)             echo sky ;;
		*sprite*|*decal*)                     echo sprites ;;
		*portal*|*mirror*|*stencil*|*clip\ plane*) echo portal ;;
		*menu*|*menudef*)                     echo menu ;;
		*font*)                               echo font ;;
		*hud*|*weapon*|*statusbar*|*sbar*)    echo hud ;;
		*interface*|*context*|*glew*|*loader*|*profile*|*extension*) echo system ;;
		*cmake*|*build*|*compile*|*msvc*|*warning*) echo build ;;
		*fixed:*|*fix\ *|*fixed\ *)           echo bugfix ;;
		*gl_scene*|*gl_walls*|*gl_flats*|*renderstate*|*vertexbuffer*|*drawinfo*) echo renderer ;;
		*)                                    echo other ;;
	esac
}

git log --reverse --format='%H%x09%cd' --date=short "$FROM..$TO" -- \
	src/gl 'wadsrc/static/shaders/*' | while IFS=$'\t' read -r full date; do
	sha=$(git rev-parse --short "$full")
	subj=$(git log -1 --format='%s' "$full" | tr '\t' ' ')
	paths=$(git diff-tree --no-commit-id --name-only -r "$full" | tr '\n' ' ')
	lc_subj=$(printf '%s' "$subj" | tr 'A-Z' 'a-z')
	cat=$(categorize "$lc_subj" "$paths")
	printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$sha" "$date" "$cat" "pending" "—" "$subj"
done
