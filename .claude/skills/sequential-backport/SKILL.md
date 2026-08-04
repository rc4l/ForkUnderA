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
- Mixed → port the slice that applies; record the part that doesn't. **This is the rule most often
  broken — see "A skip is decided per HUNK" below.**

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

## A skip is decided per HUNK, never per commit

A commit is not a unit of relevance. Its *hunks* are. "This commit is a software-renderer commit"
is a statement about its centre of mass, and centre of mass is not a verdict.

**Run `--stat` and classify EVERY file before writing a skip.** Not the title, not the first hunk,
not the impression from the diff you skimmed. If any file in the list exists in our tree, that part
has its own verdict and you owe it a sentence.

The dangerous shape is a commit that is overwhelmingly one thing plus a small slice of another:

> `2df45598d` is ~430 lines of software renderer and ~27 lines of `Line_SetPortal` map specials.
> It was skipped as "software renderer." The specials are renderer-AGNOSTIC — they live in the map
> loader, every renderer needs them, and they survive to upstream HEAD. The majority reason
> swallowed them, and the ledger row looked correct forever.

Why this failure is worse than an ordinary miss: a skip row is **terminal**. A `pending` row is a
promise to come back; a `skip` row says "checked, nothing here for us," and nobody re-opens it. The
minority slice is not deferred, it is *erased* — and the gap only surfaces years later when the
feature it belonged to is ported and silently does nothing.

So when the minority slice genuinely cannot be taken yet (it calls a header the commit doesn't add,
it needs a subsystem we haven't started), the skip may still be right — but the note must:

1. **Say the slice is not part of the skip reason.** Name it, and say it is renderer-agnostic /
   playsim / shared, so a reader cannot infer it was covered.
2. **Name what it belongs to** — the commit or feature that will carry it in.
3. **Give the re-derivation** — the grep that answers "did this ever land?" (`grep Line_SetPortal
   actionspecials.h`), so the gap is findable without re-reading the original diff.

The same applies to any majority reason, not just the renderer: "upstream P2P netcode", "Cajun
bots", "SDL backend". Each of those has swallowed a shared-file hunk at least once. When a commit
touches BOTH a subsystem we lack and files we have, the second half is a decision you make out
loud.

**Apply with `tools/apply-upstream-diff.sh`, never a bare `patch`.** Upstream's files are CRLF and
ours are LF, so a diff straight from upstream matches *nothing* and `patch` reports every hunk of
every file as rejected — which reads exactly like massive divergence and tempts you into a hand-port
or a wrong `skip`. The script strips CR for you. Real case (2014-05 batch): an 11-file refactor
reported fully rejected, one of its files a single hunk whose context was byte-identical to ours;
with CR stripped it came down to two trivial rejects, and a later commit went 17 failing files → 2.
**A 100% rejection rate is the tell — suspect line endings before you suspect divergence.**

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
- **Pull a later fix forward when it makes us MATCH upstream instead of diverge.** The default is
  strict order, but there is one exception and it is narrow. When a commit you are porting contains
  a defect, upstream fixed it later, and that fix is *clean* — small, self-contained, no dependency
  on anything between here and there — take the fix now and record both rows `ported`. The whole
  point is the verdict: `ported` means our text matches theirs, `adapted` means it does not and a
  future re-sync will conflict on that hunk forever. Trading a permanent divergence for a few
  commits of sequence-skipping is a good trade; the ledger note carries the out-of-order reason.

  **Not a licence to range ahead.** It applies only to a fix for a defect in the commit *in hand*.
  If the later commit refactors, relocates or extends anything, it is ordinary future work — port
  it when you reach it.

  **Check whether it is already here FIRST.** A repo with any history of ad-hoc backports makes
  "we don't have it yet" an unsafe assumption. This rule was written after porting a HIT*-pointer
  fix into `P_LineAttack` that `P_SpawnPuff` had already been doing for years via an earlier
  out-of-order port — a duplicate that also read the class defaults where the existing code read
  the instance. Grep for the *destination* shape, not just the source commit.
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
   `mac_build_run.sh` (fail-closed — never hand-roll `cmake --build` + manual pk3/binary copies)
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

- **Upstream version gates carry UPSTREAM's numbers.** A ported hunk containing
  `if (SaveVersion >= NNNN)` (or any other version threshold) is expressed in upstream's numbering,
  which describes *their* format history. Ours diverged the moment we serialized something they do
  not have. Porting the number verbatim is wrong whenever our line has moved past it: the gate is
  then true for every save we can load, the legacy branch becomes dead code, and existing saves get
  read in the new layout. **Translate the number to OUR version point** -- the version at which *we*
  adopted that format -- and add the entry to the SAVEVER history block in `version.h`. Real case:
  uzdoom@e718a72b4 gates the sky format at 4507; ours changed at 4512, and since `MINSAVEVER` is
  4507 the verbatim port made every loadable save take the new branch and misparse. It built and
  passed every test. Audit the whole file's gates when you touch one -- numbers at or below the
  divergence point are inherited base formats and legitimately shared; everything above must be ours.
- **`char[]` -> `FString` inside a struct:** check the struct is not `memset`/`memcpy`'d or
  bulk-cleared anywhere (an `FString` holds a pointer), and that every renamed field is still handled
  in its `Reset()`/init path. Both were clean for `level_info_t`, but they are the first things to
  verify, not assume.
- **Upstream de-virtualizes a method:** when upstream turns a `virtual` into a non-virtual wrapper
  plus a new overridable hook (e.g. `TriggerAction` → non-virtual, calling a virtual
  `DoTriggerAction`), every one of OUR subclasses still overriding the old name silently becomes a
  **shadow**: it compiles clean, and the behaviour just stops happening when called through a base
  pointer. Grep our tree for overrides of the old name and convert them in the same commit. Real
  case: `AMusicChanger` (uzdoom@e49e926bd) would have stopped changing music when fired through the
  sector-action list. Upstream may never have fixed their own instance — theirs can disappear via
  scriptification, which is not a fix we can inherit.
- **Renames / moves:** upstream path ≠ our path. Map by basename + subsystem; `git log --follow`. The
  tracker index stores as-of-commit paths, so an old commit's path may not exist at upstream HEAD *or*
  ours — resolve via the file's identity, not its string.
- **Already ported out-of-sequence:** our tree may already contain the change (ad-hoc port). Grep our
  source for it; if present, mark `ported` with the zandrox sha — don't re-port. Grep for the shape
  the change *produces*, not the commit that produced it: an earlier backport may have landed a
  later refactor that already subsumes this commit, in which case the code sits somewhere the
  original diff never touched.
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

**A row can only cite a commit that already exists, so a rebase invalidates it.** The row names its
zandrox sha, which means it is always written *after* the code commit — and rebasing the branch
(onto a moved `main`, say) rewrites every sha and leaves the rows pointing at commits that are gone.
`commit-tracker-check` catches this ("our commit ... does not exist") but only once CI runs, so:
write the rows once the branch's history is settled, and if you rebase afterwards, re-point them in
a follow-up commit before pushing. **Careful: the check PASSES LOCALLY after a rebase** -- the
pre-rebase commits survive in your clone as dangling objects, so `git cat-file -e` still finds them,
while CI's fresh checkout does not. Verify with `git merge-base --is-ancestor <sha> HEAD` for every
row instead, which is what CI can actually see.

**A `skip` or `adapted` row MUST name what would end it.** Both are conditional states, not verdicts:
a skip rests on something being absent, an adaptation on our tree differing from upstream. Record the
dependency *and*, when you know it, the upstream commit that dissolves it -- "resolved by <sha> once
we take X". Without that the row reads as settled and nobody looks again. Real case: three commits
(`03d4f23a6`, `d925279be`, `a26fbc74f`) were skipped as "GL adaptations to ZDoom's long-texture-names
change, which Zandronum never took" -- correct at the time. We have since taken that change, so all
three are portable again, and porting `03d4f23a6` is also what retires the `FindTextureByLumpNum`
adaptation. None of that was recoverable from the rows; it was only in a commit message.

**Never let a skip be recorded as `ported`.** The ledger migration (919beed) did exactly that to
those three rows, which is worse than a wrong sha: `ported` deletes the dependency *and* the reason
to re-check, so an expired skip becomes permanent silence. `tools/commit-tracker-overlap.py` catches
the blatant shape of this (a cited commit sharing no files with the upstream one) but not the subtle
one -- it is a smell detector, not a proof.

Every commit ends as a tracker row: `ported`/`adapted`/`skip`. A skip note must account for the
**whole** commit, not its majority — if part of it touched files we have, say what happened to that
part (see "A skip is decided per HUNK"). The note must cite the **check**, not a belief — `"skip: no software-renderer (r_*) in our tree as of <our-sha>"`, `"skip: VM symbols (scout
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
