#!/usr/bin/env bash
#
# Regenerate the commit tracker from the UZDoom clone.
#
# Produces two files, always in sync:
#   coverage.tsv  one row per non-merge commit, anchor..HEAD, chronological
#                 (GZDoom first, UZDoom tail). Columns: sha date title status note.
#   index.tsv     path -> the commits that touched it (space-separated shas).
#
# The anchor (ad88cfc5e, GZDoom ~1.8, 2013-12-25) is where our src/gl/ was a
# structural match to upstream and we began cherry-picking the renderer forward.
#
# RE-RUNNABLE: existing status/note are preserved per-sha, so a re-run after an
# upstream pull only appends new commits (born `pending`) and refreshes titles;
# it never wipes curation.
#
# Usage:  UZDOOM=/path/to/UZDoom ./regen.sh
set -euo pipefail
UP="${UZDOOM:-/Users/talhataj/repos/UZDoom}"
ANCHOR="${ANCHOR:-ad88cfc5e}"           # GZDoom ~1.8, 2013-12-25: where our src/gl/ matched upstream.
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TSV="$DIR/coverage.tsv"
IDX="$DIR/index.tsv"

# Floor: drop anything dated before the anchor. `${ANCHOR}..HEAD` otherwise drags in old commits
# (2009-era ZScript-VM groundwork) that merged into mainline long after the anchor. Our base is
# Zandronum 3.2.1 (ZDoom 2.8pre); we don't track anything earlier than where we started porting.
FLOOR="${FLOOR:-$(TZ=UTC git -C "$UP" log -1 --date=short --format=%cd "$ANCHOR")}"

RAW="$(mktemp)"; CUR="$(mktemp)"; trap 'rm -f "$RAW" "$CUR"' EXIT

# --- 1. clean per-commit records (record-separated so multi-line subjects can't leak) ---
# git puts a newline after each %x1e, so records 2..N start with a stray newline;
# strip everything but hex from the sha field to shed it, and require a 40-hex sha.
# Date is a UTC ISO timestamp (%cd with iso-strict-local under TZ=UTC) so it sorts to the second.
TZ=UTC git -C "$UP" log --reverse --date=format-local:'%Y-%m-%dT%H:%M:%SZ' \
    --format='%H%x1f%cd%x1f%s%x1e' "${ANCHOR}..HEAD" \
  | awk -v RS=$'\x1e' -F$'\x1f' -v floor="$FLOOR" '{
        sha=$1; gsub(/[^0-9a-f]/,"",sha); if (length(sha)!=40) next;
        if (substr($2,1,10) < floor) next;
        t=$3; gsub(/[\t\r\n]+/," ",t); sub(/^ +/,"",t); sub(/ +$/,"",t);
        printf "%s\t%s\t%s\n", sha, $2, t }' > "$RAW"

# --- 2. preserve existing curation (status,note) keyed by sha ---
[ -f "$TSV" ] && awk -F'\t' '$1 ~ /^[0-9a-f]{40}$/ {print $1"\t"$4"\t"$5}' "$TSV" > "$CUR"

# --- 3. coverage.tsv ---
{
  printf '# commit tracker | repo https://github.com/UZDoom/UZDoom | commit URL = <repo>/commit/<sha> | status = pending|ported|adapted|skip\n'
  printf 'sha\tdate\ttitle\tstatus\tnote\n'
  awk -F'\t' -v cur="$CUR" '
      BEGIN{ while((getline l < cur)>0){ split(l,c,"\t"); st[c[1]]=c[2]; nt[c[1]]=c[3] } }
      { sha=$1; s=(sha in st && st[sha]!="")?st[sha]:"pending"; n=(sha in nt)?nt[sha]:"";
        printf "%s\t%s\t%s\t%s\t%s\n", $1, $2, $3, s, n }' "$RAW"
} > "$TSV"

# --- 4. index.tsv: path -> shas that touched it (path as-of-commit; follow renames manually) ---
{
  printf '# path -> commits that touched it (space-separated shas, chronological). Paths are as-of-commit; git log --follow to trace renames.\n'
  TZ=UTC git -C "$UP" log --reverse --date=short --format=$'\x1e''%H%x1f%cd' --name-only "${ANCHOR}..HEAD" \
  | awk -v RS=$'\x1e' -v floor="$FLOOR" '
      NF { m=split($0, L, "\n"); sha=""; ok=0;
           for(i=1;i<=m;i++){ if(L[i]=="") continue;
             if(sha==""){ nn=split(L[i], h, "\x1f"); s=h[1]; gsub(/[^0-9a-f]/,"",s);
                          if(length(s)==40){ sha=s; ok=(h[2]>=floor) } continue }
             if(!ok) continue;
             p=L[i]; if(p in seen){files[p]=files[p]" "sha}
                     else {files[p]=sha; order[++k]=p; seen[p]=1} } }
      END{ for(j=1;j<=k;j++) printf "%s\t%s\n", order[j], files[order[j]] }'
} > "$IDX"

echo "coverage.tsv: $(( $(grep -c "" "$TSV") - 2 )) commit rows"
echo "index.tsv:    $(( $(grep -c "" "$IDX") - 1 )) paths"

# --- 5. progress.json: the goal bar, computed here so no client ever counts rows ---
"$DIR/progress.sh"
