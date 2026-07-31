---
name: sequential-backport
description: How to backport upstream (GZDoom/UZDoom) commits across the whole tree IN ORDER, deciding each commit's relevance DYNAMICALLY from what exists in our tree right now — never from a hardcoded "dropped features" list, because what we keep and drop keeps evolving. Use for systematic sequential porting driven by commit-tracker/, and any time you must decide whether an upstream commit is relevant to us.
---

# Sequential backporting — whole-tree, relevance-derived

The renderer staircase is the only *sequential* effort done so far (~257 ordered `src/gl` ports,
2014–2016). The rest of `commit-tracker/coverage.tsv` — ~19k commits — is untriaged. This skill is
how you work through it in order without drowning, and how you decide what even applies to us.

## The one non-negotiable rule: relevance is DERIVED, never DECLARED

Do **not** hardcode "we dropped the software renderer / FMOD / X." That list is a lie the moment the
engine evolves (we drop something new, or re-add something old). Instead, for every upstream commit
you ask one question, answered against the tree **as it is right now**:

> Does the code this commit changes still exist in *our* tree?

- Touches files/symbols we have → **candidate** (port it).
- Touches only files/symbols we don't have → **skip**, and the reason is the *check result*
  ("no `r_*.cpp` software renderer present as of `<our-sha>`"), never "we dropped it."
- Mixed → port the slice that applies; record the part that doesn't.

Because the verdict is a *function of the current tree*, a commit skipped today auto-becomes
portable the day we add that subsystem. Re-triage re-derives it; nothing is frozen truth.

## Drive from the commit tracker

`commit-tracker/coverage.tsv` — the file with **one row per upstream commit** (anchor→HEAD,
date-ordered) — is the single source of truth and the running record of this whole effort. It carries
`status` and auto-derived `tags` (subsystem, `fn:` DECORATE actions, `acs:`, `lump:`, keywords). Walk
it oldest-first so prerequisites land before dependents; use `commit-tracker/index.tsv` (`path → shas`)
to see a commit's files and the tags to fast-classify a whole subsystem at once.

**Update `coverage.tsv` for EVERY commit you process — not just the ones you port.** A commit you skip
must flip from `pending` to `skip` with its derived reason, exactly like a `ported`/`adapted` one flips
with its zandrox sha. The triage is *done* when nothing relevant is still `pending`; that's only
legible if the TSV is kept current row-by-row. Edit the row in the SAME commit that lands the code (or,
for a skip, in the batch's bookkeeping commit). After an upstream pull, `commit-tracker/regen.sh`
appends new commits as `pending` and preserves every status you've set — so the ledger never resets.

## Deciding relevance — the procedure

For upstream commit `C` (`UP` = the UZDoom clone; our source = `src/zandronum/src`):

1. **What it touches:** `git -C $UP show --stat C` (files) and the tracker's `fn:`/`acs:`/`lump:`
   tags (symbols).
2. **Existence check against our tree, now** — for each touched path/symbol:
   - *Path:* does an equivalent exist? Match by basename + subsystem, not exact path (upstream
     renames constantly — `src/gl` → `src/rendering/hwrenderer`). `git -C $UP log --follow` traces the
     rename; grep our tree for the basename.
   - *Symbol:* grep our source for the function / DECORATE action / ACS func / lump keyword it changes.
3. **Classify** → skip (nothing of ours) / candidate (ours exists) / partial (mixed). A self-contained
   feature that's *absent but not tied to any dropped subsystem* is a candidate you port, not a product
   decision to surface — port it and move on.
4. **For candidates, hand off to `upstream-port`:** run `backport-scout.sh` → pick the route
   (staircase batch / post-wall C++ / scriptified / born-in-ZScript) → port or adapt → its gates.

## "Do we already have it?" is the FIRST check — and it's content-based

The verdict comes from the **diff and our tree, never the commit title.** "Fixed: Rampage timer…"
tells you nothing about whether we have the fix; only grepping our tree does. The test:

> Take the commit's distinctive **added lines** and `git grep -F` them anywhere under
> `src/zandronum` (substring, whitespace-stripped → survives reindentation and renames). If they're
> already there, we have it → `ported`. If the change isn't there but the files are → `pending`
> candidate. If the files themselves are absent → `skip`.

**Base-inheritance is the default below our base date.** Our sim base is ZDoom 2.8pre `458e1b1`
(2014-05-08), so most commits before that are already in our tree via inheritance — the first 100
triaged as **57 ported / 41 skip / 2 real ports.** Do not "port" what we've had for a decade; verify
presence first.

`commit-tracker/triage.py` mechanizes the first pass — merge detection, file-existence, and the
added-line presence ratio — emitting `ported` / `skip` / `candidate` / `REVIEW`. Run it, trust the
clear verdicts (all-present, all-absent, merge), and **hand-examine every `REVIEW`, `candidate`, and
partial** against the real diff. Two shapes it can't call alone, both seen in the first 100:

- **Partial inheritance:** a commit touches two files; one half is in our tree, the other isn't
  (`2501dc6df`: the ACS `args[1]>=0` guard was inherited, but our `P_FindUniqueTID` still has the old
  `limit=INT_MAX` signed-overflow UB the commit rewrote). → port the missing half only.
- **Adapted-present:** the feature exists via a *different* implementation (`a60918f60`:
  `disablepushwindowcheck` is present as `COMPATF2_PUSHWINDOW`, not upstream's `BCOMPATF_NOWINDOWCHECK`).
  → `ported`/`adapted`, do **not** re-port and conflict.

## Order & dependencies

- **One commit at a time, oldest→newest. Never bundle commits into a "flight" or "cluster."** Each
  upstream commit was built and shipped on its own upstream, so ported in order, individually, each
  one builds — the *ordering* supplies the dependency (the commit that introduces a symbol lands
  before the commit that uses it). A big multi-commit refactor is still done one commit at a time; it
  is not a reason to batch. (Flights are a renderer-staircase cherry-pick optimization — not this.)
- **Skipping a prereq can break a later dependent** (it references a symbol the skipped commit
  added). When a commit won't apply cleanly, check whether it leans on a skipped one; port the
  minimal prerequisite or adapt around it. Don't blind-skip a whole subsystem without checking who
  downstream depends on it.

## Cadence — batch, push, prove green, then advance

Never pile up a mountain of unverified ports. Port **one commit at a time** (each its own commit with
its ledger row); a "batch" here is only a **push/CI grouping** — a handful of *already-finished*
individual ports pushed together so CI runs once, never a bundle of commits ported as a unit. After
each port:

1. **Commit per verified step** — one upstream commit (or one coherent flight) per commit, with its
   tracker row updated in the same commit.
2. **Local first:** `mac_compile.sh` builds, `ctest` green, then rebuild+refresh the bundle with
   `tools/build-run.sh` (fail-closed — never hand-roll `cmake --build` + manual pk3/binary copies)
   and do the manual E2E for anything runtime-visible (per the `upstream-port` gates).
3. **Push the branch and prove CI green on ALL platforms.** A **draft PR is the CI vehicle** — a bare
   branch push skips the Linux/Windows build jobs; only a PR triggers them (plus the tripwires,
   `commit-tracker-check`, tests). MSVC catches ODR/ABI bugs ELF/Mach-O swallow, so "green on my Mac"
   is not green.
4. **Do not start the next batch until every check is green.** A red build compounds — the next port
   buries the cause. Fix or revert the batch first; `main` and the branch must always build.
5. **Checkpoint-merge to `main` periodically** once green, in small reviewable increments, so `main`
   never carries a broken or half-triaged state.

This is the loop: derive-relevance → port a batch → local build+tests → push → all-platform CI green →
merge checkpoint → next batch. Sequential means *verified* sequential, not fast and reckless.

## Edge cases — cover all of them

- **Renames / moves:** upstream path ≠ our path. Map by basename + subsystem; `git log --follow`. The
  tracker index stores as-of-commit paths, so an old commit's path may not exist at upstream HEAD *or*
  ours — resolve via the file's identity, not its string.
- **Already ported out-of-sequence:** our tree may already contain the change (ad-hoc port). Grep our
  source for it; if present, mark `ported` with the zandrox sha — don't re-port.
- **Reverted / superseded upstream:** the change was undone or replaced by a later commit. Check it
  survives to upstream HEAD (`git -C $UP log --oneline -- <path>`); if it was reverted, skip with that
  reason.
- **Merge commits:** no unique content (their changes live in the parents) → skip as topology.
- **VM / ZScript:** relevance-negative, but DERIVED via the scout tripwire detecting VM symbols — not a
  feature list. Post-2016 DECORATE (`thingdef/*`) is VM-backed → route as scriptified/born-in-ZScript,
  never a raw cherry-pick.
- **The float-sim wall (2016–17):** later renderer commits assume `DVector` positions. Caught by the
  fixed64 audit and the strong `zx::Fixed` type at compile time — you detect it from the code, not a
  hardcoded cutoff date.
- **Cross-cutting refactors** (mass rename, API change, "major cleanup of the texture manager"): touch
  both kept and dropped code. Port only the slice that maps onto files we have; the seam catalog
  (`docs/hwrender-portability-scope.md`) is the map of where whole-adoption goes wrong.
- **Subsystems we added, upstream lacks** (Vulkan backend, Zandronum netcode/lumps/ACS funcs): upstream
  commits won't touch them, but our divergence means upstream's surrounding context may not apply —
  hand-merge, don't expect a clean patch.
- **Platform-only commits** (win32/cocoa/sdl launcher, build files): relevant only if we ship that
  platform surface — again, check whether the touched file exists in our tree, don't assume.
- **Relevance drift:** we add/remove a subsystem later → the existence check flips. Never treat a
  `skip` as permanent; the note must record the *missing dependency* so a re-triage can re-verify.
- **Empty / whitespace / doc-only commits:** skip, reason recorded.

## Recording (so the verdict is auditable and re-derivable)

Every commit ends as a tracker row: `ported`/`adapted`/`skip`. The note must cite the **check**, not a
belief — `"skip: no software-renderer (r_*) in our tree as of <our-sha>"`, `"skip: VM symbols (scout
tripwire)"`, `"ported: 9811962"`. A future reader (or a re-triage after the tree changes) can then
re-run the same existence check and confirm or overturn it. That is what keeps the whole thing from
silently rotting into a stale drop-list.

## Zandronum client/server adaptation — see the `netcode-adaptation` skill

Our single biggest porting hazard: ZandroX is **client/server**; GZDoom is not. A gameplay change
that's correct upstream can **desync multiplayer** and pass a single-player build+run clean — the bug
is invisible offline. **Any port touching actor state, movement/collision, spawning, targeting/AI,
player state, RNG, or sound must go through the `netcode-adaptation` skill before it lands.** In short:
server-gate the authoritative logic with `if (NETWORK_InClientMode() == false)`, broadcast the result
with the matching `SERVERCOMMANDS_*`, keep the sync RNG (`P_Random`) intact, and add byte/bit-exact
wire-format regression tests. Such a port is recorded `adapted` (note the gate), never `ported` raw,
and verified with a **multiplayer** E2E — never single-player alone.

**Special case — the upstream commit is *itself* a netcode fix.** Upstream is peer-to-peer lockstep;
we're client/server. A commit fixing `d_net.cpp` / the ticcmd transport patches a mechanism Zandronum
replaced → `skip: upstream P2P-lockstep netcode, not our C/S model`. Only if the underlying gameplay
bug also reproduces under client/server is it real — and then you don't port the diff, you write the
C/S equivalent per `netcode-adaptation`. The tell is *where the diff lands* (transport → skip; `p_*`
actor state → maybe adapt), never the title.

## Gates (per candidate)

The `upstream-port` gates apply unchanged — tripwires, fixed64 audit, build-everywhere (CI), tests,
manual E2E, commit-per-verified-step, plain messages / no attribution. Update the tracker row in the
same commit (the proactive-removal discipline). See also `provenance-links` for the `<engine>@<sha>`
tag on every ported unit.
