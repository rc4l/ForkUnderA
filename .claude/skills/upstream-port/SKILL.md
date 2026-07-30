---
name: upstream-port
description: How to port anything from GZDoom/UZDoom into ZandroX — staircase renderer batches, post-wall C++ features, scriptified features, and born-in-ZScript features — using the features/ pattern, the tripwire, the Rosetta index, and the fixed64 + manual-E2E verification gates. Use whenever bringing upstream code, features, or fixes into this engine.
---

# Porting upstream (GZDoom/UZDoom) work into ZandroX

ZandroX is a fixed-point (64-bit 48.16), client/server engine. Upstream is float-sim and
ZScript-heavy. Every port goes through ONE of four routes, decided by the scout — never freehand.
Prior art and rationale: `docs/renderer-staircase.md`, `docs/zscript-insulation.md`,
`docs/zscript-deep-checks.md`, `docs/hwrender-portability-scope.md` (the seam catalog — read §12
before touching textures).

## Step 0 — always run the scout first

```
tools/backport-scout.sh /Users/talhataj/repos/UZDoom <upstream path>
```

It answers: VM-tainted or clean, scriptification ancestor (Rosetta), and the delta since. Regenerate
the index after upstream pulls: `tools/zscript-rosetta-gen.sh <clone> > tools/data/zscript-rosetta.tsv`.

When porting a specific fix rather than a whole file, trace it to its origin commit (`cd $UP && git log --oneline -- <path>` or `git log -S<symbol>`) and port that change with its rationale — don't reason from the HEAD snapshot alone.

## Step 0.5 — the commit tracker is the coverage ledger

`commit-tracker/coverage.tsv` holds one row per upstream commit from our parity anchor
(`ad88cfc5e`) to UZDoom HEAD, each marked `pending` / `ported` / `adapted` / `skip`. Before
porting, look the commit up there; when a port lands, set its row (`ported` = faithful/re-diffable,
`adapted` = our own reimplementation, `skip` = won't take — note why). A deliberately scoped-down
port is `adapted` or stays `pending` with the leftover in the note — **never** `ported`; that is how
silent scope-narrowing is caught. To find which commits touched a file, use `commit-tracker/index.tsv`
(`path → shas`) — see `commit-tracker/README.md` for the query recipes. Re-run `commit-tracker/regen.sh`
after an upstream pull; it appends new commits as `pending` and preserves every status you've set.

## The four routes

1. **Staircase batch** (renderer commits, 2013-12→2016-01 window): cherry-pick the upstream commits
   of one flight (see the flight table in `docs/renderer-staircase.md`), hand-merge around the
   [BB]/[AK] Zandronum divergence. The window is verified ZScript-free; run the tripwire anyway.
2. **Post-wall clean C++** (scout says CLEAN, e.g. `common/rendering`): adapt directly, but check
   the seam catalog first — never adopt their scene layer, texture model (`FGameTexture`), or frame
   loop wholesale; the hybrid attempt's 15 seams are the map of what goes wrong.
3. **Scriptified feature** (Rosetta has an ancestor): take the C++ ancestor at `<commit>~1` as the
   skeleton, translate the ZScript delta since back into C++. The scriptification commit is a 1:1
   translation — use it as the Rosetta stone for idiom mapping.
4. **Born-in-ZScript** (no Rosetta record): the ZScript file IS the spec. Rewrite in C++ from
   reading it. Natives it calls usually still exist in our tree under old names; grep before
   reimplementing.

## Where the code lands (the features/ pattern — `src/zandronum/src/features/README.md` is law)

- New feature → `features/<kebab-name>/` with a README listing every in-place engine edit.
- Pure decisions → `features/<name>/computation/<x>_compute.{h,cpp}` + colocated `_test.cpp`.
  Auto-globbed into engine and tests — **must be header-pure** (no engine/GL/SDL includes) and
  C++14-clean (no namespace-scope `inline constexpr` variables).
- Feature glue `.cpp` → add to `add_executable( zdoom … )` **before `zzautozend.cpp`** (the creg
  link-order rule); never a trailing `target_sources` for anything with `IMPLEMENT_CLASS`.
- In-place edits to existing files stay in place; enumerate them in the feature README.
- Staircase batches are the exception: they modify `gl/` in place, mirroring upstream's own diffs.

## The non-negotiable gates, in order

0. **Proactive enforcement (user directive)**: any flight/port that REMOVES an API or dependency
   must, in the same commit, (a) append the removed API's call pattern to
   `tools/removed-api-tripwire.sh` and (b) sever its linkage in the build — the linker is part of
   the tripwire. Never rely on one platform's strictness to catch stragglers (the gluPerspective
   lesson: mac/linux tolerated a missed call for a full CI round because GLU was still linked
   there; only Windows enforced it).
1. **Tripwires**: `tools/zscript-tripwire.sh`, `tools/removed-api-tripwire.sh`, and
   `tools/ff-parity-tripwire.sh` (all in CI). ZScript false-positive traps already excluded:
   `GC::WriteBarrier` (2008 GC, ours) and `DEFINE_ACTION_FUNCTION` (classic DECORATE macro). If a
   ported file needs its VM surface stripped, the F2DDrawer precedent says script exports are
   typically self-contained blocks under ~6% of the file. FF-parity (the white-decal lesson,
   flight 6): upstream freely moves state into shader-only uniforms because it dropped GL 2.x at
   the core flip — we keep fixed-function alive on macOS until OUR core flip, so any render-state
   field consumed only in `ApplyShader()` needs an FF fallback (`[rc4l]` shim in `Apply()`, see
   the mObjectColor precedent) or a justified allowlist entry in the tripwire.
2. **fixed64 audit** (see the `fixed64-widening` skill): every fixed→float crossing goes
   `int64 → double → float` (`features/hwrender/computation/vertexconvert_compute` is the tested
   reference); grep applied hunks for `fixed_t`, `FRACBITS`, `<<16`, `(int)` casts. Upstream code
   declaring its own `fixed_t` MUST defer to `basictypes.h` — the strong type catches collisions at
   compile time; treat any such error as a real finding, not noise.
3. **Build everywhere**: local `mac_compile.sh`, then CI (draft PR triggers Linux/Windows — branch
   pushes alone skip the build jobs). MSVC flags are spelled per-compiler; MSVC also catches real
   ODR bugs ELF/Mach-O swallow — same-name classes get a `Legacy` prefix rename (precedent:
   `LegacyFRenderState`, `LegacyFlatVertexBuffer`).
4. **Tests**: `cmake --build build-tests && ctest` all green; new computation units at 100%
   coverage (`bash tests/coverage.sh --auto`).
5. **Manual E2E by the user is the verification standard** (their eye has overruled screenshot
   reads repeatedly). Drive the engine with the `zandronum-driver` skill; remember the THREE stale
   layers after any change: `cmake --build build`, copy `build/zandronum` AND `build/zandronum.pk3`
   into `build/ZandroX.app/Contents/MacOS/`, re-codesign; wadsrc edits additionally need the pk3
   deleted first (the `add_pk3` trap). E2E must inspect the *artifacts an action leaves behind*,
   not just the action itself: fire at a wall and then WALK UP to the decals; kill and look at the
   corpse; open a door and look at the track (the white-decal miss — the muzzle flash verified,
   the bullet marks it left didn't). And cover the views nobody defaults to: LOOK STRAIGHT UP at
   the sky (`warp -600 1800` on MAP01 + `+lookup` — the white-zenith miss), look down from
   ledges, check a mirror/portal when the flight touches stencils. `warp x y` teleports; use it
   instead of walking. For visual deltas, A/B against the previous flight: `git stash` +
   rebuild beats reasoning from memory (screenshots of both sides settle it in minutes).
6. Commit per verified step, plain messages, no attribution (user's global rules). Do not merge
   WIP branches; draft PRs are the CI vehicle.

## Hard prohibitions

- No ZScript VM code compiled into the engine, ever (`docs/zscript-insulation.md`).
- No float-sim adoption; the sim stays fixed-point — conversions are draw-side and one-way.
- No post-2016 `thingdef/*` cherry-picks (upstream DECORATE is VM-backed after 2016-10).
- No second render pipeline coexisting with the first (the 15-seam lesson).

## Ledger (staircase flights)

Staircase flights record their status in the repo-wide commit tracker (see Step 0.5), same as any
other port — mark each upstream sha `ported`/`adapted`/`skip` in `commit-tracker/coverage.tsv` in the
SAME commit that lands the code, with your zandrox sha in the note. `tools/commit-tracker-check.sh`
(CI) guards the file's format and that every ported/adapted row's provenance commit exists. Strategy
and the flight table live in `docs/renderer-staircase.md`.
