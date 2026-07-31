#!/usr/bin/env python3
"""
One-time migration: add the `ours` column (field 6) to coverage.tsv.

`sha` (field 1) is THEIR commit (upstream). `ours` is OUR repo commit(s) that
addressed the row -- a comma-separated list of zandrox shas, or a keyword.

Value rules (rigid, no fuzzy matching):
  - pending / skip           -> "/"            (inapplicable: nothing of ours addressed it)
  - ported / adapted:
      * explicit our-sha in the note           -> that sha (note "abc1234 ..." or "ported abc1234 ...")
      * a known real port with no sha in note  -> SPECIAL[sha]
      * everything else (base-inherited /
        adapted-present, "present N/M", etc.)  -> "zandronum-base"

Notes are left untouched; `ours` is the new provenance source of truth.
Env OURS_580 overrides the our-commit for uzdoom@580094a7 (the PSX/Doom64 port).
"""
import os, re, sys

LEAD_SHA   = re.compile(r'^([0-9a-f]{7,40})(?:\s|$)')
PORTED_SHA = re.compile(r'^ported\s+([0-9a-f]{7,40})', re.I)

# Real ports whose note carries no sha (filled in when the landing commit is known).
SPECIAL = {
    "580094a7924ea586bc0c789e55d51aa3dc3f2287": os.environ.get("OURS_580", "PENDING_116"),
}

def ours_for(sha, status, note):
    if status in ("pending", "skip"):
        return "/"
    if status in ("ported", "adapted"):
        if sha in SPECIAL:
            return SPECIAL[sha]
        m = LEAD_SHA.match(note) or PORTED_SHA.match(note)
        return m.group(1) if m else "zandronum-base"
    return "/"

def main():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "coverage.tsv")
    if len(sys.argv) > 1:
        path = sys.argv[1]
    out, tally = [], {}
    with open(path) as f:
        for i, ln in enumerate(f):
            ln = ln.rstrip("\n")
            if i == 0:  # provenance/comment header
                out.append(ln + " | ours = OUR commit(s), '/' if pending/skip, 'zandronum-base' if base-inherited")
                continue
            if i == 1:  # column header
                out.append("sha\tdate\ttitle\tstatus\tnote\tours")
                continue
            c = ln.split("\t")
            if len(c) < 5:
                out.append(ln); continue
            if len(c) >= 6:  # already migrated -- keep existing ours
                out.append(ln); tally[c[5]] = tally.get(c[5], 0) + 1; continue
            sha, date, title, status, note = c[0], c[1], c[2], c[3], c[4]
            ov = ours_for(sha, status, note)
            out.append("\t".join([sha, date, title, status, note, ov]))
            key = ov if ov in ("/", "zandronum-base") else ("<our-sha>")
            tally[key] = tally.get(key, 0) + 1
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")
    print("migrated", path)
    for k in sorted(tally):
        print("  %-12s %d" % (k, tally[k]))

if __name__ == "__main__":
    main()
