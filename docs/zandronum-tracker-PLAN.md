# Zandronum commit tracker — implementation plan

Status: **walked** (2026-11-12): every row resolved, counts in `commit-tracker/zandronum/progress.json` · Owner: rc4l · One branch, one PR, merged with a merge commit

A second commit tracker, built the same way as the UZDoom one, with one row per Zandronum commit
from our 3.2.1 base to the tip of `zandronum-stable`. Every row ends in a verdict, and the tracker
takes new commits as Zandronum publishes them.

---

## 0. What this plan relies on (checked 2026-11-11)

| Fact | Value |
|---|---|
| Upstream | `https://foss.heptapod.net/zandronum/zandronum-stable`, branch `default`. It is the only public engine repo in the Zandronum group, and `default` is the 3.3-alpha line (`GAMEVER_STRING "3.3"`, `-alpha`). |
| Base | `ZA_3.2.1` = Mercurial `a701547f4162` (rev 11885) = git `28f736fb3` in the [TorrSamaho/zandronum](https://github.com/TorrSamaho/zandronum) mirror, which is what our subtree squash `cee4608` names. |
| Range | `::default - ::ZA_3.2.1`: **235 commits**, linear, no merges (tip `f7888a42ab35`, 2026-10-23). |
| Git mirror | Its `master` stops at the 193rd commit (2026-06-06) and has not been pushed since 2026-06-13. |
| Transplants | **64** commits carry `transplant_source`, meaning they were `hg transplant`ed from `zandronum/gzdoom-hg-mirror`. 56 resolve to one UZDoom commit by author timestamp, 7 need a title tie-break, and 1 came from inside Zandronum itself. Their UZDoom rows: 11 ported, 2 adapted, 1 skip, **42 pending**, 5 with no row. |
| Zandronum's own | 170 commits (116 by Adam Kaminski): netcode, ACS functions, DECORATE properties, server cvars, bots. Plus 1 tag commit. |
| Already ours | 15 have `zandronum@` provenance tags in our tree, mostly the zstd series from PR #387. Two of those are transplants. Others were taken without tags (#393, #410). |
| Changelog noise | `docs/zandronum-history.txt` is touched by 77 of the 235. Zandronum's backport notes (`docs/zandronum-uzdoom-backport-nodes.txt`, ~1200 lines) list the hunks it could not apply. |
| Documented skips | No auth server (removed), the changelog is not maintained past 3.2.1, and stock 3.2.1 and 3.3 peers are refused at connect (`features/net-transport/README.md`). |

## 1. Decisions

- **D1. Rows are keyed by Mercurial node.** Heptapod's commit pages and API use node ids, and they are
  40 hex characters, so the format check passes unchanged. The mirror's git shas cannot be the key
  because the newest 42 commits have none. The 15 existing `zandronum@<git sha>` tags are rewritten to
  nodes once, using `zandronum/aliases.tsv` (mirror git sha to node, 193 rows matched one-to-one by
  date and title). From then on there is one sha space.
- **D2. Rows are in revision order, and the goal is a row, not a date.** Transplants keep their
  2014–2018 dates, so sorting by date would put them first, and a date goal would count them under any
  goal. `progress.sh` takes a goal row and counts the rows up to it in file order. That still gives the
  same numbers for UZDoom, whose file is already in date order. The Zandronum goal is "caught up to
  `default`" until 3.3 ships and is tagged.
- **D3. One toolset, two ledgers.** A `commit-tracker/ledgers.json` describes each ledger: VCS, clone
  path, range, ordering, commit and file URL templates, goal, ignored paths, and its twin ledger.
  UZDoom's files stay where they are, so the public site's URLs do not change. Zandronum's live in
  `commit-tracker/zandronum/`. Copying the folder is ruled out by AGENTS.md.
- **D4. Twins.** Each transplanted row gets a `twin` column holding the UZDoom sha it came from. A
  column, not a note, because the check enforces it. The UZDoom row and its Zandronum twin must agree:
  both pending, or both resolved citing the same `ours` sha. A twin is ported once, at its Zandronum
  position, even when that is ahead of the UZDoom walk's frontier. Zandronum already met its
  prerequisites on the same ZDoom 2.8pre base.
- **D5. Theirs only if it is the better design; otherwise ours wins.** This applies wherever
  Zandronum's commit overlaps something we already have: our own feature, our netcode modules, or a
  twin we ported from UZDoom. "Better" is judged against criteria we can check, in this order:
  1. AGENTS.md: logic in pure `Compute` helpers testable without the engine, generic code over
     per-case boilerplate, and `features/` packaging.
  2. Server-authoritative, with wire-format tests (`netcode-adaptation`, `netcode-testing`).
  3. Fewer special cases and globals; fits the subsystem it lands in (our transport, decals `Maker`,
     gamecopy and so on).
  4. Measured, when performance is the claim (`fuactl abperf`).

  When the call is unclear, ours wins. The note says which side won and why, in one line: an
  `ours-wins:` or `theirs-wins:` token.
- **D6. Same six statuses.** `pending ported adapted skip deferred partially-deferred`. How they map
  here:
  - **`ported`**: Zandronum's change as written, re-diffable against it.
  - **`adapted`**: the change is in, reshaped for our tree. This includes fixed64 widening and
    netcode gating. Also used when we already had the behaviour in a better design: `ours` names our
    commit(s), and the note carries `ours-wins:`.
  - **`skip`**: the code the change touches does not exist in our tree. The note gives the existence
    check, for example "auth server removed, `features/net-transport/README.md`".
  - **`deferred`**: it applies to us, but a written policy holds it back. The first deferral under a
    new policy adds that policy to the README's policy table, with the doc that holds it.
  - **`partially-deferred`**: part ported, part held back. The note says what landed and what was
    refused.
  - **Hunks, not commits, decide a verdict.** Ignored paths do not count: the changelog, the
    backport-notes docs, and `.hgtags`.
- **D7. One PR for the program**, phases as commits, merged with a merge commit. A squash merge
  would orphan every sha the rows cite, and `commit-tracker-check` rejects that.

## 2. Phase A — tooling that works for both ledgers

| # | Change | Notes |
|---|---|---|
| A1 | `ledgers.json` | Clone paths default to sibling folders (`../UZDoom`, `../Zandronum`), overridable by env. This also removes the personal Mac path that `regen.sh`, `gentags.py`, `topics.py`, `lumps.py` and `triage.py` default to today. |
| A2 | `commit-tracker/vcs.py` | One adapter with git and hg backends: list the commits in a range in ledger order, the files of a commit, its diff, a file at a revision, transplant source. Parsing lives in pure functions tested against recorded `git log`/`hg log` text (`tools/commit_tracker_vcs_test.py`), so CI needs no Mercurial. Only the subprocess calls are thin glue. |
| A3 | `regen.sh` | Becomes `regen.sh --ledger NAME` on top of `vcs.py`. Curation is preserved per sha, exactly as today. New rows are born `pending`. Each run resolves twins for new transplants. Curated rows whose sha has left upstream history are listed loudly and kept, as with the README's migrated-ledger orphans. |
| A4 | `progress.sh` | Goal by row (D2). |
| A5 | `gentags.py`, `gazetteer.py`, `lumps.py`, `topics.py`, `triage.py` | Take `--ledger` and use `vcs.py`. `triage.py` applies the ignored paths and reports per hunk. |
| A6 | `tools/commit-tracker-check.sh`, `commit-tracker-overlap.py` | Loop over ledgers. The check validates the `twin` column and twin agreement. It runs Python as `${PYTHON:-python3}`, so the local Windows run stops failing on the Store stub. |
| A7 | `commit-tracker-convergence.py` | Reads file content through `vcs.py`, so it works for both ledgers. `convergence.yml` (on demand only) installs Mercurial with pip for the Zandronum ledger. |
| A8 | Golden check | `regen --ledger uzdoom`, pinned to the current tail `7bfbf612`, must reproduce today's `coverage.tsv`, `index.tsv`, `files.tsv` and tag files byte for byte. That proves the refactor changed nothing. |

## 3. Phase B — the Zandronum ledger

1. Clone `zandronum-stable` to `../Zandronum` (`F:\Zandronum` here). Mercurial is at
   `F:\ShitDronum\Mercurial\hg`, so set `HG` or add it to PATH.
2. Run `regen.sh --ledger zandronum` to write `commit-tracker/zandronum/` (`coverage.tsv`, `index.tsv`,
   `files.tsv`, the tag files, `progress.json`, `aliases.tsv`). Expect 235 rows today, 64 of them with
   a `twin`.
3. Add Zandronum-flavoured tags in `gentags.py` for both ledgers: `svc:` for server commands, `cvar:`
   for cvars, `acs:` for ACS functions. Most of this ledger is netcode, so filtering by command or cvar
   is the useful view.
4. Parse Zandronum's backport notes into a `left-out` field per twin: the hunks Zandronum could not
   apply because the file did not exist in Zandronum. We may have those files from our UZDoom ports,
   so the walk checks each one.

## 4. Phase C — the page

- **One viewer.** `index.html` gets a ledger switcher (UZDoom | Zandronum) that reads `ledgers.json`.
  `?ledger=zandronum` deep-links to it at `commit-tracker.cantstopscrolling.net/?ledger=zandronum`,
  loading `zandronum/coverage.tsv` and the rest.
- **Per-ledger behaviour, all driven by the config:**
  - commit and file links: Heptapod `/-/commit/<node>` and `/-/blob/<node>/<path>`;
  - default sort: file order for Zandronum, date for UZDoom;
  - progress bar from that ledger's `progress.json`;
  - header text naming the base;
  - a `twin` column whose link jumps to the UZDoom row.
- **Cross-links from UZDoom rows.** A UZDoom row with a Zandronum twin shows "Zandronum took this as
  `<node>`", derived from the Zandronum ledger.
- **Publishing.** `pages.yml` already publishes everything under `commit-tracker/`, so it needs no
  change.
- **Verify locally.** Serve the folder, load both ledgers in Chrome at desktop and phone widths, and
  check the deep link, row detail, twin jump and progress bar. No CI step for this.

## 5. Phase D — skills, CLI and docs, before the walk starts

**`fuactl tracker`** is the front door for the walk. It is Node, in `tools/fuactl/src/tracker.mjs`
with tests, and calls `vcs.py` for history. It finds a real Python, skipping the WindowsApps stub.

| Subcommand | What it does |
|---|---|
| `next [--ledger L]` | The first pending row. Shows its files with in-our-tree flags (ignored paths applied), twin and twin status, Zandronum's left-out hunks, `zandronum@` tags already in our tree, the line-presence hint, and the diffstat. |
| `show <sha>` | The same, for any row. |
| `diff <sha> [--apply]` | The diff minus ignored paths. `--apply` runs it through `tools/apply-upstream-diff.sh` (CR stripping, `-N`, zero `.rej`). |
| `mark <sha> <status> [--ours s,...] [--note ...]` | Edits the row, and the twin row with it. Validates as the check does, then reruns `progress.sh`, so `progress.json` can never go stale from a hand edit. |
| `pull [--ledger L]` | `hg pull` or `git fetch`, regen, twins, tags, progress, check. |
| `check` | `commit-tracker-check` for every ledger. |

**Skills:**

- **`sequential-backport`**: written for ledgers in general. A Zandronum section covers revision
  order, twins, D5, ignored paths, left-out hunks, and the documented skips (auth server, changelog,
  stock-peer compatibility). It names `fuactl tracker` as the loop.
- **`upstream-port`**: a Zandronum route. Step 0.5 names both ledgers. Zandronum code still assumes a
  32-bit `fixed_t`, so the `fixed64-widening` audit applies to every Zandronum port.
- **`provenance-links`**: `zandronum@` takes a `zandronum-stable` Mercurial node. Mirror git shas are
  legacy and resolved through `aliases.tsv`. A twin ported from Zandronum's version is tagged
  `zandronum@<node>`, and its ledger row carries the UZDoom link.
- **`netcode-adaptation` and `netcode-testing`**: Zandronum's own netcode commits are already
  server-authoritative but target its own wire and transport. Port the behaviour, keep our transport,
  add wire tests, and judge with `fuactl netplay` and `netdiff`.
- **`fixed64-widening`**: lists Zandronum ports as a source of 32-bit assumptions.
- **`fua-naming`**: identifiers that come from Zandronum keep Zandronum's names. `fua_` is only for
  what we invent.
- **Dev mode**: any test or debug cvar or command a Zandronum commit adds is flagged
  `CVAR_FUA_DEV_MODE` or `DEV_MODE_CCMD` (AGENTS.md). `sv_dumppackets` is the first case to recheck.

**Docs:** a "Ledgers" section in `commit-tracker/README.md` (both ledgers, the `twin` column, the
policy table, D5 tokens) and `README.fuactl.md` for `fuactl tracker`.

## 6. Phase E — first pass, then the walk

**First pass (one bookkeeping commit).** Only verdicts backed by evidence: 1 tag commit `skip`,
14 twins inheriting their UZDoom verdicts, and the 15 tag-attributed rows. Two of those overlap, so
**28 rows** in all. Each tagged row's `ours` is the commit that added its tag (`git log -S`). The same
commit rewrites the 15 legacy tags to nodes (D1). The line-presence ratio is a hint shown by
`fuactl tracker next` and never written as a verdict: it scored the ported zstd commit at 27%.

**The walk.** The remaining ~207 rows, one at a time, in revision order. For each commit:

1. `fuactl tracker next`. Classify every file and hunk, ignored paths aside.
2. Check whether it is already here, by content and by the shape it produces. Look for an ad hoc
   port, a twin we took, or our own design covering it.
3. Decide using D5 and D6. Partial means the skipped slice is named, with what would bring it in
   (the per-hunk rule).
4. **To port:**
   - Apply with `fuactl tracker diff --apply`.
   - Gameplay and netcode go through `netcode-adaptation`.
   - Run the fixed64 audit.
   - New C++ gets `Compute` helpers and 100% coverage (`fuactl coverage --since`). Each crash found
     gets a failing test first.
   - Build with `fuactl build`.
   - Runtime-visible changes get a manual E2E: scenario, netplay or template map, as the change calls
     for.
   - Tag `zandronum@<node>`.
   - Commit with `fuactl tracker mark` in the same commit, which updates the twin row too.
5. **To skip or defer:** `fuactl tracker mark`, batched into bookkeeping commits.
6. **Push in batches.** CI must be green on every platform before the next batch starts.

Expected kinds of commit, from the 2026-11-11 survey:

- Zandronum's own netcode (movement command buffers `93c67c50`, net graph `9a8ae9d8`, userinfo
  cleanup), judged under D5 against our netcode modules.
- The 2015 `FTagManager` transplant series. It is pending in the UZDoom ledger too and right at its
  frontier.
- ACS functions and DECORATE properties.
- Bot content removals: decide D5 or a policy at the first one.
- Zandronum CI and AppImage packaging: an existence check decides.

## 7. Phase F — staying current

- `fuactl tracker pull --ledger zandronum` brings in new commits. Rows are born `pending` and twins are
  resolved. A new tag such as `ZA_3.3` moves the goal row.
- **If Zandronum rebases onto UZDoom**, which its backport notes say it intends, the import arrives
  mostly as transplants. Their twins land on rows the UZDoom ledger has already decided, so the bulk
  of it resolves through D4.
- **If a curated node disappears upstream**, `pull` keeps the row and reports it. It never deletes
  curation silently.
- No scheduled CI job: `pull` is run by hand, as with `regen.sh` today.

## 8. Order of commits in the PR

1. A1–A8 (golden UZDoom output identical).
2. B1–B4 (Zandronum ledger, check clean).
3. C (page).
4. D (fuactl tracker with tests, skills, docs).
5. E first pass, including the tag rewrite.
6. E walk batches.
7. Retro.
