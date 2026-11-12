# Commit tracker

Per-commit record of what we've pulled from upstream into ZandroX, so a half-done port can never
masquerade as finished and nothing gets silently skipped. There are two **ledgers**, one per upstream
(see [Ledgers](#ledgers)): UZDoom (GZDoom → UZDoom) at the top of this folder, and Zandronum in
`zandronum/`. Everything below describes the UZDoom ledger unless it says otherwise; the Zandronum
one has the same files and rules.

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
| `coverage.tsv` | the tracker — one row per commit, `sha date title status note ours` (+ `twin` in Zandronum's) |
| `index.tsv`    | `path → the commits that touched it` (for "what changed file X?")        |
| `files.tsv`    | `commit → the paths it touched` (the viewer's file list, the overlap check) |
| `tags.tsv`, `combined_tags.tsv` | derived tags: paths and `fn:`/`svc:`/`clc:` (`gentags.py`); lumps, keywords, `acs:`, `cvar:` (`gazetteer.py`) |
| `progress.json`| the goal bar's numbers, precomputed — the viewer draws them, never counts rows |
| `ledgers.json` | each ledger's upstream, VCS, clone, range, links, goal, ignored paths and twin |
| `tracker.py`   | the command line (`fuactl tracker` runs it): regen, progress, check, next, show, diff, mark, pull |
| `ledger.py`, `vcs.py`, `overlap.py` | the shared rules, git/Mercurial history, and the overlap check; tested by `tools/commit_tracker_test.py` |
| `regen.sh`, `progress.sh` | wrappers for `tracker.py regen` / `progress` |
| `index.html`   | AG Grid viewer — a ledger switch, status filters, search, commit links (GitHub Pages-ready) |

## coverage.tsv columns

`sha` `date` `title` — copied verbatim from `git log`, never hand-edited (a re-run refreshes them).
`date` is a canonical UTC ISO timestamp (sorts to the second); the viewer shows it in the reader's
local time. `status` `note` `ours` — the three fields you curate.

`sha` (field 1) is **their** commit (upstream UZDoom). `ours` (field 6) is **our** repo commit(s)
that addressed the row — the provenance the guard enforces instead of parsing it out of the note:

| `ours` value                | meaning                                                            |
|-----------------------------|-------------------------------------------------------------------|
| `/`                         | inapplicable — the row is `pending` or `skip` (nothing of ours)   |
| `zandronum-base`            | present in the Zandronum 3.2.1 baseline import (squash `cee4608`, from upstream `28f736fb3`) — no discrete backport commit of ours |
| `<sha>[,<sha>…]`            | one-or-more zandrox commits that ported/adapted it (comma-separated) |

Commit URL = `https://github.com/UZDoom/UZDoom/commit/<sha>` (their); `ours` links to
`https://github.com/rc4l/ForkUnderA/commit/<sha>`.

## the goal bar

The viewer shows one number: **percent of commits handled, up to a goal we pick**. Handled means
any status other than `pending` — `skip` counts, because a reviewed-and-rejected commit is done
with, not outstanding. The goal is a release we're aiming at, named by its **row**: every row from
the top of `coverage.tsv` down to it is in scope, the rest waits until we move the goal. A row and
not a date, because Zandronum's ledger is in revision order and its transplants keep their
2014–2018 dates.

It is computed in the repo by `progress.sh` and shipped as `progress.json`. Clients never
calculate it — one definition of progress, and the bar paints instantly instead of waiting on
the ~5 MB of TSV behind the table. `commit-tracker-check` fails when it is stale.

**To retarget it**, edit the ledger's `goal` in `ledgers.json` (`"sha": "tip"` means the last row)
and re-run `./progress.sh`. UZDoom's goal row is `1f09341d2`, the last commit dated 2016-02-23.

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
| `skip`    | reviewed, nothing of it applies to our tree. Note = the existence check that says so. |
| `deferred` | it applies to us, and a written policy holds it back for now. Note = `deferred: <policy>`, what it would give us, and the doc that holds the policy. |
| `partially-deferred` | part landed, part held back by a policy. Note = both halves. |

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
npx fuactl tracker pull                   # fetch both upstreams, regen, tags, progress, check
./regen.sh --ledger zandronum             # or one step by hand (UZDOOM=/ZANDRONUM= override the clones)
```

Clones default to `../UZDoom` and `../Zandronum` beside this checkout. New commits are appended as
`pending`, titles refresh, and every status/note/ours/twin you've already set is preserved (keyed by
sha). A curated row whose commit has left upstream history moves to the ledger's `orphans.tsv`; it is
never dropped silently.

## Ledgers

| ledger | upstream | rows | order | goal |
|---|---|---|---|---|
| `uzdoom` (this folder) | github.com/UZDoom/UZDoom, git | anchor `ad88cfc5e` → HEAD | date | GZDoom 2.1.1 |
| `zandronum` (`zandronum/`) | foss.heptapod.net/zandronum/zandronum-stable `default`, Mercurial | our base `ZA_3.2.1` → tip | revision | the tip, until 3.3 is tagged |

The page shows one at a time: `?ledger=zandronum`. Zandronum's rows are keyed by Mercurial node.
The TorrSamaho/zandronum git mirror stopped at 2026-06-06; `zandronum/aliases.tsv` maps its git
shas, which older tags and commit messages of ours cite, to nodes.

**Twins.** 64 Zandronum commits were `hg transplant`ed from GZDoom/UZDoom. Their `twin` column names
the UZDoom commit (from Zandronum's backport notes, else by author time and title), and the check
holds the pair to one verdict: the same status and `ours`, except that one side may stay `pending`
while the other is `adapted` or `partially-deferred`, since those say part of the change was not
taken. `fuactl tracker mark` sets both rows at once. A twin is ported once, at whichever row the
walk reaches first. `zandronum/left_out.tsv` lists the hunks Zandronum's own notes say it could not
apply; we may have those files, so the walk checks them.

**Ignored paths.** In Zandronum's ledger, `docs/zandronum-history.txt` (77 of the first 235
commits touch it), the backport-notes files and `.hgtags` never decide a verdict and never tag a row.

**Which design wins.** Where a Zandronum commit overlaps something we already have — our own
feature, our netcode, a twin we took from UZDoom — Zandronum's version is taken only if it is the
better design, judged in this order: logic in `Compute` helpers testable without the engine, generic
over per-case code, `features/` packaging (AGENTS.md); server-authoritative with wire tests; fewer
special cases and a fit with the subsystem it lands in; measured, when speed is the claim. When it
is unclear, ours wins. The note opens with `ours-wins:` or `theirs-wins:` and says why in a line;
`ours-wins:` rows are `adapted`, citing our commit(s).

**Policies.** A `deferred` row names the policy that holds it back:

| policy | held in | refuses |
|---|---|---|
| `zscript` | `docs/zscript-insulation.md` | anything that needs the ZScript VM |

A Zandronum row is never `zandronum-base`: every one of them is after our base, so the check
rejects it. A full `<ledger>@<node>` provenance tag whose first 12 digits match a row must carry
that row's node, so a tail typed by hand fails the check rather than reading as real. Documented existence checks for Zandronum skips: the auth server was removed, the
changelog is not maintained past 3.2.1, and stock Zandronum peers are refused at connect
(`src/zandronum/src/features/net-transport/README.md`).

## Migrated-ledger orphans

Three rows from the old `progress/renderer-staircase/ledger.tsv` referenced merge commits that
upstream history later rewrote away, so they aren't ancestors of HEAD and have no row here. Kept
for the record:

| upstream sha | old status | note |
|--------------|-----------|------|
| `b6f8a45bb` | skip (sky)      | Vavoom skybox missing-texture check; our code uses a different (already-correct) `maplump==-1` form — no-op. |
| `6ac3e4ce4` | deferred (sprites) | Fly-cheat/projectile-spawn fix needs `MF7_FLYCHEAT` + the coordinate refactor (thingpos) — #41. |
| `a21044791` | ported (bugfix) | fixed: fake contrast was not applied (our commit `9d12147`). |

## Not here

`tools/data/zscript-rosetta.tsv` stays separate — it's a lookup index (scriptification commit → the
C++ ancestor files) the backport scout uses, not a status tracker.
