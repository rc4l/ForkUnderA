# ZScript insulation strategy (2026-07-23)

**Goal:** ZandroX must be insulated against every vector by which ZScript can enter — because the
netcode replicates a closed, hand-instrumented function set and can never host the VM
(`docs/zscript-feasibility.md`). This is policy plus **mechanical enforcement**
(`tools/zscript-tripwire.sh`), not vigilance.

## The timeline (mined from UZDoom/GZDoom history)

| Date | Event | Commit |
|---|---|---|
| 2016-10-12 | ZScript work begins (DECORATE separation) | `b1a83bfd2` |
| 2016–2018 | Peak entanglement: 316 / 387 / 380 scripting commits per year | — |
| 2017-02-04 | Menus begin scriptification | `d5b908186` |
| 2017-03-22 | Statusbar scriptified (incl. the SBARINFO wrapper) | `9bffe4ee5` |
| 2020-04-11 | 2D drawer moved to `common/` carrying VM exports | `5fe22c70b` |
| 2023-08-02 | First VM contact in the hw renderer proper (weapon bob) | `bcbb85b1d` |

**Verified: the renderer staircase window (2013-12-25 → 2016-01-01) has ZERO ZScript contact** —
no commit in the window mentions ZScript or touches a scripting directory. The staircase is clean by
construction.

## The seven vectors, and the gate on each

1. **Staircase batches (2013–16).** Clean (verified above). Gate anyway: run the tripwire on every
   cherry-picked batch — zero-cost tripword against mis-dated picks.
2. **Post-wall code adoption** (vendored backend, any future hop). Measured: `common/rendering` has
   0 VM references even at 2026 trunk; the scene layer has exactly one file (weapon bob, 2023).
   Gate: any file taken from post-2016 upstream passes the tripwire first; VM surfaces are stripped
   (the F2DDrawer precedent: script exports were 10 self-contained blocks, 5.6% of the file).
3. **Shared-system rider commits** (textures/fonts/sbar/menu companion track). In-window = clean.
   Post-2016 shared-system commits are off the menu unless tripwired + stripped.
4. **DECORATE itself became ZScript-backed upstream (2016-10).** Curve ball: any post-2016 upstream
   fix to `thingdef/*` assumes the VM compiler. Gate: our classic DECORATE interpreter is the
   permanent implementation; post-2016 thingdef commits are reimplemented, never cherry-picked.
5. **Mod ecosystem pressure** (the real-world vector: mods ship `zscript` lumps). Strategy: the
   transpiler program (feasibility study §3) for the convertible subset + demand-driven
   codepointer/ACS growth. Engine gate: fail *gracefully* on `zscript` lumps — a clear load-time
   error naming the lump and pointing at the converter, never a crash or silent ignore.
6. **Asset lumps** (menudef/sbarinfo/gameinfo borrowed from upstream reference `zandronum.pk3`s
   post-2017 reference `class` names of script types). Gate: borrowed lumps are audited; our
   parsers only accept our C++ types (the hybrid effort proved our MENUDEF engine reads the shared
   DSL — data yes, script classes never).
7. **Vendored reference trees** (`rendering/`, `ZVulkan/`). Exempt from the tripwire ONLY while
   outside the build; the moment any file enters the engine's source list it must pass the gate.

## The tripwire (mechanical enforcement)

`tools/zscript-tripwire.sh` — greps compiled engine code for **ZScript-era-only** symbols
(`VMFunction`, `VMValue`, `IFVIRTUAL`, `ZCC_`, …), calibrated to zero hits on the clean tree and
validated to catch real post-wall VM code. Two deliberate exclusions, learned the hard way on first
run: `GC::WriteBarrier` (ZDoom's 2008 DObject garbage collector — native, pre-ZScript, ours) and
`DEFINE_ACTION_FUNCTION` (the classic DECORATE codepointer macro whose *name* ZScript later
reused). Run it: per staircase batch, per borrowed file, and as a CI step.

## The one-line policy

**Nothing the engine compiles may reference the VM; everything mods want from ZScript arrives by
conversion onto our hand-instrumented DECORATE/ACS surface — and a machine, not a person, checks.**
