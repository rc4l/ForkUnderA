#!/usr/bin/env bash
#
# Generate commit-tracker/tags.tsv  ->  sha \t space-separated tags
#
# Tags are DERIVED, never hand-written:
#   * category tags   from the paths a commit touches (renderer/gl/vulkan/zscript/acs/...)
#   * fn:A_* symbols  from A_Xxx action-function tokens on the changed lines of the gameplay
#                     dirs where DECORATE actions live. Pattern-discovered, so a new action
#                     upstream is picked up automatically (no list to maintain).
#
# Two passes: a fast --name-only pass (categories, all files) and a targeted -p pass
# (symbols, gameplay paths only) so we never diff the whole tree.
#
# Usage:  UZDOOM=/path/to/UZDoom ./gentags.sh
set -euo pipefail
UP="${UZDOOM:-/Users/talhataj/repos/UZDoom}"
ANCHOR="${ANCHOR:-ad88cfc5e}"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FLOOR="${FLOOR:-$(TZ=UTC git -C "$UP" log -1 --date=short --format=%cd "$ANCHOR")}"
OUT="$DIR/tags.tsv"
CATS="$(mktemp)"; SYMS="$(mktemp)"; trap 'rm -f "$CATS" "$SYMS"' EXIT

# where DECORATE action functions are defined/called (keeps the -p diff small)
GAMEPLAY=(src/g_doom src/g_heretic src/g_hexen src/g_strife src/g_shared src/g_raven
          src/thingdef src/scripting src/p_enemy.cpp src/p_mobj.cpp src/p_pspr.cpp
          wadsrc/static/zscript)

# --- pass A: category tags from touched paths (all files, fast) ---
# UNTIL bounds both passes to a date window (e.g. the first N commits) for a quick partial run.
git -C "$UP" log --no-renames --name-only --format=$'\x01''%H%x1f%cs' ${UNTIL:+--until="$UNTIL"} "${ANCHOR}..HEAD" \
| awk -v floor="$FLOOR" '
    # readable name for a ZDoom-convention filename prefix (fixed source convention, not per-feature)
    function readable(x) {
      if(x=="p")return"playsim"; if(x=="g")return"game";   if(x=="r")return"swrender";
      if(x=="s")return"sound";   if(x=="d")return"main";   if(x=="m")return"misc";
      if(x=="c")return"console"; if(x=="v")return"video";  if(x=="i")return"system";
      if(x=="w")return"wad";     if(x=="f")return"finale"; if(x=="b")return"bot";
      if(x=="a")return"actors";  if(x=="hu")return"hud";   if(x=="st")return"statusbar";
      if(x=="am")return"automap";if(x=="po")return"polyobj"; return x;
    }
    function addcat(p,  q,dir) {
      # cross-cutting semantic tags (additive)
      if (p ~ /vulkan/)                                     t["vulkan"]=1;
      if (p ~ /(\/|^)(gl|hwrenderer)\// || p ~ /rendering\//) t["renderer"]=1;
      if (p ~ /\.zs$/ || p ~ /zscript/)                     t["zscript"]=1;
      if (p ~ /decorate|thingdef/)                          t["decorate"]=1;
      if (p ~ /p_acs/)                                      t["acs"]=1;
      # structural auto-tag: guarantees every touched file yields a tag
      if (p ~ /CMakeLists|\.cmake$/) { t["build"]=1; return }
      if (p ~ /^\.github\//)         { t["ci"]=1;    return }
      if (p ~ /^docs\//)             { t["docs"]=1;  return }
      q=p; sub(/^src\//,"",q); sub(/^wadsrc\/static\//,"",q); sub(/^wadsrc\//,"",q);
      if (q ~ /\//) { dir=q; sub(/\/.*/,"",dir); t[dir]=1; return }   # first dir component
      if (match(q,/^[a-z0-9]+_/)) { t[readable(substr(q,1,RLENGTH-1))]=1; return }
      sub(/\.[^.]+$/,"",q); if (q!="") t[q]=1;                        # flat file, no prefix -> stem
    }
    # every non-skipped commit emits a tag; a commit with no files (a merge) is tagged "merge"
    function emit(  k,o){ if(sha==""||skip)return; o=""; for(k in t) o=o (o==""?"":" ") k;
                         if(o=="")o="merge"; print sha"\t"o }
    /^\x01/ { emit(); sub(/^\x01/,""); n=split($0,a,"\x1f"); sha=a[1]; skip=(a[2]<floor); delete t; next }
    skip { next }
    /./ { addcat($0) }
    END { emit() }' > "$CATS"

# --- pass B: fn:A_* symbols from changed lines in the gameplay dirs (targeted -p) ---
# Per-commit diffing over the whole history is a long batch (~30 min); SYM_SINCE/SYM_UNTIL can
# bound it to a window for a fast partial run (categories still cover every commit).
# Pre-filter with grep (robust to the very long minified/data lines that make BWK awk fatal):
# keep only commit markers and changed lines that actually contain an A_ token; truncate as a backstop.
git -C "$UP" log --no-renames -p --unified=0 --format=$'\x01''%H%x1f%cs' \
    ${SYM_SINCE:+--since="$SYM_SINCE"} ${SYM_UNTIL:+--until="$SYM_UNTIL"} \
    "${ANCHOR}..HEAD" -- "${GAMEPLAY[@]}" \
| { grep -aE $'^\x01|^[-+].*A_[A-Z]' || true; } \
| cut -c1-4000 \
| awk -v floor="$FLOOR" '
    function emit(  k,o){ o=""; for(k in t) o=o (o==""?"":" ") k; if(sha!=""&&o!="") print sha"\t"o }
    /^\x01/ { emit(); sub(/^\x01/,""); n=split($0,a,"\x1f"); sha=a[1]; skip=(a[2]<floor); delete t; next }
    skip { next }
    /^[+-]/ { if ($0 ~ /^(\+\+\+|---)/) next; s=$0;
              while (match(s,/A_[A-Z][A-Za-z0-9]+/)) { t["fn:" substr(s,RSTART,RLENGTH)]=1;
                                                       s=substr(s,RSTART+RLENGTH) } }
    END { emit() }' > "$SYMS"

# --- merge the two per-sha tag sets ---
{
  printf '# sha -> derived tags (space-separated). categories from touched paths; fn:A_* from diff tokens.\n'
  awk -F'\t' '
    FNR==NR { cat[$1]=$2; next }
            { if ($1 in cat) { print $1"\t"cat[$1]" "$2; done[$1]=1 } else print $1"\t"$2 }
    END { for (s in cat) if (!(s in done)) print s"\t"cat[s] }' "$CATS" "$SYMS"
} > "$OUT.tmp"
mv "$OUT.tmp" "$OUT"
echo "tags.tsv: $(( $(grep -c "" "$OUT") - 1 )) tagged commits  (cats $(wc -l < "$CATS"), syms $(wc -l < "$SYMS"))"
