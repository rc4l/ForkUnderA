# Commit tracker

Per-commit record of what we've pulled from upstream (GZDoom → UZDoom) into ZandroX, so a
half-done port can never masquerade as finished and nothing gets silently skipped.

One row per upstream commit, from our **parity anchor** forward to UZDoom HEAD. The anchor is
`ad88cfc5e` (GZDoom ~1.8, 2013-12-25) — the point where our `src/gl/` was a structural match to
upstream and we began cherry-picking the renderer forward. Everything the anchor already contains
is our inherited baseline and isn't tracked. Commits are chronological, so **GZDoom-era rows come
first and the UZDoom tail (after 2025-10) comes last**.

## Files

| file           | what it is                                                              |
|----------------|-------------------------------------------------------------------------|
| `coverage.tsv` | the tracker — one row per commit, `sha date title status note`          |
| `index.tsv`    | `path → the commits that touched it` (for "what changed file X?")        |
| `regen.sh`     | rebuilds both from the UZDoom clone; re-runnable, never wipes curation   |

## coverage.tsv columns

`sha` `date` `title` — copied verbatim from `git log`, never hand-edited (a re-run refreshes them).
`status` `note` — the only two fields you curate.

Commit URL = `https://github.com/UZDoom/UZDoom/commit/<sha>`.

## status vocabulary

| status    | meaning                                                                        |
|-----------|--------------------------------------------------------------------------------|
| `pending` | not yet reviewed (default; this is the "not covered" pile)                      |
| `ported`  | brought over faithfully — re-diffable against upstream. Note = our commit sha.  |
| `adapted` | took the idea, built our **own** version (ZScript/float/incompatible → reimplemented in C++). Deliberately diverged, so it can't be re-diffed. Note = our commit sha + what diverged. |
| `skip`    | reviewed, not porting. Note = why (noise, VM-only, already in base, rejected).  |

A deliberately scoped-down port is **not** `ported` — it's `adapted` (a smaller version on purpose)
or stays `pending` (debt), with the leftover named in the note. That rule is the whole point.

## Queries

```sh
# coverage counts
cut -f4 coverage.tsv | tail -n +3 | sort | uniq -c

# everything not yet handled
awk -F'\t' '$4=="pending"' coverage.tsv

# everything done
awk -F'\t' '$4=="ported" || $4=="adapted"' coverage.tsv

# which commits touched a file — and our status on each
grep -P '^src/gl/scene/gl_flats\.cpp\t' index.tsv        # -> the shas
# join to coverage for status:
grep -oP '(?<=\t).*' <(grep -P '^src/rendering/hwrenderer/hw_vertexbuilder\.cpp\t' index.tsv) \
  | tr ' ' '\n' | grep -Ff - coverage.tsv
```

Paths in `index.tsv` are **as-of-commit** — upstream renamed files over the years, so to trace a
file across a rename use `git -C <UZDoom> log --follow -- <path>` and look the shas up in coverage.

## Regenerating

```sh
UZDOOM=/path/to/UZDoom ./regen.sh
```

Re-run after an upstream pull: new commits are appended as `pending`, titles refresh, and every
status/note you've already set is preserved (keyed by sha).

## Migrated-ledger orphans

Three rows from the old `progress/renderer-staircase/ledger.tsv` referenced merge commits that
upstream history later rewrote away, so they aren't ancestors of HEAD and have no row here. Kept
for the record:

| upstream sha | old status | note |
|--------------|-----------|------|
| `b6f8a45bb` | skip (sky)      | Vavoom skybox missing-texture check; our code uses a different (already-correct) `maplump==-1` form — no-op. |
| `6ac3e4ce4` | deferred (sprites) | Fly-cheat/projectile-spawn fix needs `MF7_FLYCHEAT` + the coordinate refactor (thingpos) — #41. |
| `a21044791` | ported (bugfix) | fixed: fake contrast was not applied (our commit `9811962`). |

## Not here

`tools/data/zscript-rosetta.tsv` stays separate — it's a lookup index (scriptification commit → the
C++ ancestor files) the backport scout uses, not a status tracker.
