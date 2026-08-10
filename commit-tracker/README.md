# Commit tracker

Per-commit record of what we've pulled from upstream (GZDoom → UZDoom) into ZandroX, so a
half-done port can never masquerade as finished and nothing gets silently skipped.

One row per upstream commit, from our **parity anchor** forward to UZDoom HEAD. The anchor is
`ad88cfc5e` (GZDoom ~1.8, 2013-12-25) — the point where our `src/gl/` was a structural match to
upstream and we began cherry-picking the renderer forward. Our engine base is **Zandronum 3.2.1**
(ZDoom 2.8pre @ `458e1b1`); everything the anchor already contains is inherited baseline and isn't
tracked, and rows dated before the anchor are floored out (old ZScript-VM branches that merged into
mainline years later). Commits are chronological, so **GZDoom-era rows come first and the UZDoom
tail (after 2025-10) comes last**.

## Files

| file           | what it is                                                              |
|----------------|-------------------------------------------------------------------------|
| `coverage.tsv` | the tracker — one row per commit, `sha date title status note ours`     |
| `index.tsv`    | `path → the commits that touched it` (for "what changed file X?")        |
| `regen.sh`     | rebuilds both from the UZDoom clone; re-runnable, never wipes curation   |
| `progress.sh`  | computes progress toward the current goal into `progress.json` (run by `regen.sh`) |
| `progress.json`| the goal bar's numbers, precomputed — the viewer draws them, never counts rows |
| `index.html`   | AG Grid viewer — status filters, search, sortable, commit links (GitHub Pages-ready) |

## coverage.tsv columns

`sha` `date` `title` — copied verbatim from `git log`, never hand-edited (a re-run refreshes them).
`date` is a canonical UTC ISO timestamp (sorts to the second); the viewer shows it in the reader's
local time. `status` `note` `ours` — the three fields you curate.

`sha` (field 1) is **their** commit (upstream UZDoom). `ours` (field 6) is **our** repo commit(s)
that addressed the row — the provenance the guard enforces instead of parsing it out of the note:

| `ours` value                | meaning                                                            |
|-----------------------------|-------------------------------------------------------------------|
| `/`                         | inapplicable — the row is `pending` or `skip` (nothing of ours)   |
| `zandronum-base`            | present in the Zandronum 3.2.1 baseline import (squash `b9495b8`, from upstream `28f736fb3`) — no discrete backport commit of ours |
| `<sha>[,<sha>…]`            | one-or-more zandrox commits that ported/adapted it (comma-separated) |

Commit URL = `https://github.com/UZDoom/UZDoom/commit/<sha>` (their); `ours` links to
`https://github.com/rc4l/ForkUnderA/commit/<sha>`.

## the goal bar

The viewer shows one number: **percent of commits handled, up to a goal we pick**. Handled means
any status other than `pending` — `skip` counts, because a reviewed-and-rejected commit is done
with, not outstanding. The goal is a release we're aiming at; everything dated after it is out of
scope for the bar until we move the goal.

It is computed in the repo by `progress.sh` and shipped as `progress.json`. Clients never
calculate it — one definition of progress, and the bar paints instantly instead of waiting on
the ~5 MB of TSV behind the table.

**To retarget it**, edit the two constants at the top of `progress.sh` and re-run it:

```sh
GOAL_LABEL="GZDoom 2.1.1"   GOAL_DATE="2016-02-23"   ./progress.sh
```

| goal | date | note |
|---|---|---|
| GZDoom 2.0.05 | 2014-12-27 | **reached 2026-08-04**, 1179/1179 |
| GZDoom 2.1.1  | 2016-02-23 | **current**; the renderer half is gated on the base-engine backport (#41) |
| GZDoom 2.3    | 2016-09-19 | after — the last release *before* ZScript exists |

2.1.1 was chosen over jumping straight to 2.3 so the sim lands where the renderer staircase
already stopped (`db766f9fe`, 2016-02-03) and near the Cocoa backend target (`108dcf122`,
2016-09), rather than leaving three subsystems at three different points in upstream history.

**Where ZScript actually starts.** Not at the VM — that lands 2014-12-20 (`2d87eb0ba`) and is
just infrastructure. The ZScript *language* arrives at `433bf4601` (2016-10-13), the commit that
converts `inventory.txt`/`player.txt`/`specialspot.txt` to ZSCRIPT and drops them from the
DECORATE include list. `g2.3pre` does not contain it; `g2.4pre` does. So **GZDoom 2.4 is the
first release with ZScript**, and 2.3 is the wall to walk to — see `deferred` /
`partially-deferred` below and `docs/zscript-insulation.md`.

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
