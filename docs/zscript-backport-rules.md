# Handling ZScript-era commits (2026-08-04)

Rules for the sequential backport when it reaches upstream's scripting work. Read
`.claude/skills/sequential-backport` and `.claude/skills/netcode-adaptation` first; this only covers
what is special about these commits.

## The decision

**Intake every commit, in order, faithfully.** No skipping the VM, no "engine-only subset". Upstream
built this incrementally over years and the tree stays coherent if we follow the same order.

The frontier is at 2014-05. The VM arrives before ZScript does: `PType` (2013) → VM + codegen
backend (early 2016) → **DECORATE recompiled onto the VM** (2016) → ZScript as a second frontend
(2016-10) → data scopes (2017-02).

## Where the code goes

- **Ported upstream code lands in place**, mirroring upstream's own path (`src/zandronum/src/scripting/`).
  It is a vendored port, not a feature — same as staircase batches editing `gl/` in place.
  `features/` is for things we invent (`features/README.md`).
- **`jit/` and `dap/` are not ported.** ~16k lines, optional (JIT compiler, debug adapter). Mark
  those rows `skip` with the reason.

## The netcode rule, in one line

Upstream is peer-to-peer: every peer runs every line of script, so nothing is replicated. We are
client/server: the client only knows what the server tells it.

That difference shows up in exactly one place — **a client reading simulation state.** Everything
else ports unchanged.

### Three cases, in order of how often they come up

1. **Script runs on the server and mutates the sim.** The existing rules apply, nothing new: server
   authority + `SERVERCOMMANDS_*` per `netcode-adaptation`. When DECORATE is recompiled onto the VM
   (2016), our replication calls live *inside* the action functions — making those functions
   VM-callable changes the calling convention, not the replication. The 61 calls in
   `thingdef_codeptr.cpp` still fire. **Port these commits normally.**

2. **Client code reads an engine field** (health, armor, ammo, weapon, frags). **Already replicated**
   — Zandronum has sent these for twenty years because the C++ HUD needs them. Nothing to do.

3. **Client code reads a field a mod invented.** This is the only genuinely new case. A mod adds
   `int heatLevel` to its own weapon and draws a heat bar; the field exists only on the server and
   nothing knows to send it. Hand-written commands cannot solve this — the field does not exist
   until someone loads a wad.

Case 3 is the one piece of original work in this whole effort. It is a **generic script-field
replication channel**: the server walks each object's declared fields, diffs them against a
per-client baseline, and sends what changed. Written once; works for mods that do not exist yet.
It goes in `features/zscript-replication/` with its `computation/` units and tests. **Do not start
it until case 3 actually appears** — it cannot, until scopes land in 2017-02.

## Scopes: port them faithfully

When `play` / `ui` / `clearscope` arrive (2017-02, `0f031c5f2`), port `FScopeBarrier` as written.
Do not flatten or simplify it. It is compile-time *and* runtime enforced, and for us it does a second
job upstream never needed: **the set of things `ui` may read from `play` is the list of what a client
must be sent.** That list is why case 3 is tractable — we inherit it instead of designing it.

Before 2017-02 there are no scope qualifiers. Treat all script as `play` (server) for that window
and let the 2017 commits introduce the split.

## One VM

There is no "server VM" and "client VM" — one VM, one port, compiled into the engine. Scope decides
what executes where at runtime: the server runs `play` classes, the client runs `ui` classes.

## Tripwire

`tools/zscript-tripwire.sh` currently fails CI on any VM symbol in `src/zandronum/src`. It gets
narrowed to a path allowlist (VM symbols permitted under the scripting path, banned elsewhere) on the
first commit that needs it. Do not delete it — it still catches accidental VM creep in unrelated
ports, which is what it was built for.

## Per-commit checklist

1. Classify against the three cases above. Most commits are case 1 or "no netcode surface at all".
2. Port faithfully; in-place path mirroring upstream.
3. `fixed64-widening` rules at every fixed↔double crossing — the sim stays fixed-point, script sees
   doubles, conversion happens at the boundary.
4. Provenance comment per `provenance-links`.
5. Update `commit-tracker/coverage.tsv` in the same commit.
6. Build **all** targets, not just `zdoom` (`windows_build_run.ps1` / `mac_build_run.sh`).

## Known open questions

Not blockers for porting, but unanswered — revisit when case 3 is real:

- Untrusted mod script on a public server (runaway loops, memory). ACS has instruction budgets;
  ZScript's story is unchecked.
- VM + GC inside a 35 Hz server tick with N clients.
- Bandwidth budget when a mod declares many replicated fields on many actors.
- Late joiners need a full snapshot, not deltas.
- Weapon logic in script round-trips to the server — a feel problem, not a correctness one.
- C++14 vs the scripting stack's C++20 (isolatable per-file per `docs/zscript-deep-checks.md`,
  unconfirmed for the scope/backend code).

Background: `docs/zscript-feasibility.md`, `docs/zscript-deep-checks.md`, `docs/zscript-crossengine.md`.
