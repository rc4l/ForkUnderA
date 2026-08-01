---
name: netcode-adaptation
description: How to adapt a gameplay change to ZandroX's client/server netcode when backporting or writing engine code — the server-authority + SERVERCOMMANDS broadcast pattern Zandronum established, the case-by-case rules for when a change needs gating, and the byte/bit-exact wire-format regression tests that keep multiplayer from silently desyncing. Use for ANY port or edit touching actor state, movement, spawning, AI, player state, RNG, or sound.
---

# Adapting gameplay to ZandroX client/server netcode

ZandroX (like Zandronum) is **client/server**; GZDoom/ZDoom upstream is not. A gameplay change that's
correct upstream can **desync multiplayer** here — and it passes a single-player build+run clean, so
the bug is invisible offline. This skill is how you decide whether a change needs netcode work, how to
do it the way Zandronum itself did, and how to lock the wire format so it stays correct.

This applies to **any** code touching actor state, movement/collision, spawning, targeting/AI, player
state, RNG, or sound — whether backported (see `sequential-backport`, `upstream-port`) or written fresh.

## The canonical pattern (how Zandronum actually did it)

The `[BB]`/`[BC]`/`[AK]`-tagged code throughout `p_*` follows one shape: the **server runs the
authoritative logic and broadcasts the result; clients only apply what they receive.**

```cpp
// [rc4l] server-authoritative -- clients receive the SERVERCOMMANDS; they never run the logic.
if ( NETWORK_InClientMode() == false )        // == server + single-player
{
    ...the gameplay mutation (spawn / move / set state / damage / sound)...
    if ( NETWORK_GetState() == NETSTATE_SERVER )
        SERVERCOMMANDS_XXX( actor, ... );
}
```

`NETWORK_InClientMode()` is the dominant gate (79 uses in `p_enemy.cpp` alone). `NETWORK_GetState()`
returns `NETSTATE_SERVER` / `NETSTATE_CLIENT` / `NETSTATE_SINGLE`.

## The rules — in order, case by case

1. **Does it mutate synced state?** position/velocity, state/frame, flags, health/damage, spawn,
   destroy, target, sound. **If not** (enum constants, build files, offline UI, DECORATE/lump parsing,
   texture names) → no netcode work, port/write as-is. Most changes are this — don't over-gate.
2. **Server-gate the authoritative logic** with `if (NETWORK_InClientMode() == false)`, so a client
   never independently runs it — that independent run *is* the desync.
3. **Broadcast the result** with the matching existing `SERVERCOMMANDS_*` under
   `NETWORK_GetState() == NETSTATE_SERVER`. The catalog covers nearly every mutation:
   - **Movement/position:** `MoveThing`, `MoveThingExact`
   - **State/animation:** `SetThingState`, `SetThingFrame`
   - **Flags/orientation:** `SetThingFlags`, `SetThingAngle`
   - **Spawn:** `SpawnThing`, `SpawnMissile`, `SpawnPuff`
   - **Destroy:** `DestroyThing`
   - **Sound:** `SoundActor`

   Use the existing command; a mutation with **no** matching command is a bigger job (a new net
   command *and* its matching client-side handler in `cl_main`/`network` decode), not a quiet
   cherry-pick.
4. **RNG discipline:** only the server draws the sync RNG (`P_Random`/`pr_*`) for authoritative
   outcomes; a client drawing it independently drifts. Non-sync RNG (`M_Random`) is for cosmetic-only
   paths. Match how the surrounding code draws it.
5. **Prediction:** player movement/collision runs through `CLIENT_PREDICT_*` — the change must hold
   under predict-then-correct, not just locally.
6. **Spectators / clientside:** paths gated by `bSpectating` / `CLIENTSIDEONLY` where neighbours are.
7. **Mirror the neighbour.** Zandronum is internally consistent — the code *right next to* your hunk
   already shows how that kind of mutation is gated + broadcast. Grep the same file for
   `NETWORK_InClientMode` / `SERVERCOMMANDS_` near the change and copy that shape; tag your adaptation
   `[rc4l]` (as they tagged theirs `[BB]`/`[BC]`/`[AK]`).

## When the upstream commit is *itself* a netcode fix

Upstream (ZDoom/GZDoom/UZDoom) netcode is **peer-to-peer deterministic lockstep** — every peer runs
the full sim and they exchange inputs/ticcmds. Zandronum **threw that out** and rewrote it as
client/server authoritative (`network.cpp`, `sv_*`, `cl_*`, `SERVERCOMMANDS_*`). They are different
systems solving the same problem, not two versions of one. So a commit titled "fix netcode desync / net
packet / prediction" almost always patches the **P2P-lockstep mechanism we don't have** — its diff
lands in `d_net.cpp` / the ticcmd transport, files we inherited from the ZDoom base but diverged past.

Handle it in two steps:
1. **Transport, or gameplay bug that merely surfaced over the net?** If the fix is to the
   lockstep/transport mechanism itself → `skip: upstream P2P-lockstep netcode, not our C/S model
   (network.cpp / sv_*/cl_*)`. Porting the diff patches a mechanism that isn't live here — worse than
   useless.
2. **If the underlying gameplay bug also reproduces under client/server** (an actor ends up in the
   wrong state, a value desyncs regardless of transport) → it's real, but **do not port upstream's
   diff.** Re-express the fix in our terms: server-authoritative logic gated with
   `NETWORK_InClientMode()`, broadcast via the matching `SERVERCOMMANDS_*`, tested per the wire-format
   rules below. Record it `adapted`, never `ported`.

The tell is *where the diff lands*, not the commit title: `d_net.cpp`/ticcmd/`NetUpdate` internals →
transport (skip); `p_*` actor/gameplay state → possibly real (adapt).

## Composition — this skill chains with the porting skills

A change rarely arrives pre-labelled "netcode." It reaches this skill *through* another:
- **`sequential-backport`** derives relevance, then routes any commit touching synced state here.
- **`upstream-port`** does the C++ translation (staircase / post-wall / scriptified / born-in-ZScript);
  if the ported result mutates synced state, this skill governs the C/S adaptation of that result.
- **ZScript features are not exempt.** A scriptified or born-in-ZScript feature (upstream-port routes 3
  and 4) that mutates actor state gets reverse-translated to C++ **and then** adapted here — the ZScript
  origin changes *how you get the C++*, not whether it needs server authority. Do both, in that order.

## Wire-format regression tests — the bytes/bits must match, exactly

**See the `netcode-testing` skill for how to write these** (the `zandrox_tests_net` target, golden +
round-trip + adversarial patterns, the bit/byte checklist). The rule below is *what* to test; that
skill is *how*.

A multiplayer E2E proves it *works once*; it does not lock the **wire format**. Every `SERVERCOMMANDS_*`
serializes a fixed byte layout — a command id then `NETWORK_WriteByte`/`WriteShort`/`WriteLong`/
`WriteFloat`/`WriteString` fields — that the client decodes field-for-field in the same order. If a
change shifts a field's type, order, count, or the command id, **the client silently misreads every
following byte** in that packet: a corruption no compiler and no single-player run can catch. So for any
change that adds or alters a net command (or the state one carries):

- **Happy-path serialization test:** build the command for a known actor/state, capture the emitted
  byte buffer, and assert the **exact bytes/bits** — id, each field's width, and their order. This is
  the contract; pin it.
- **Regression / golden test:** keep the expected buffer as a golden fixture so a later refactor that
  reorders or re-types a field fails loudly instead of desyncing in the field. Round-trip it too:
  write → read back → assert the decoded struct equals the input (writer and reader must agree on
  widths — a `WriteShort` paired with a `ReadByte` is exactly the class of bug this catches).
- **Boundary / parity cases:** signedness and truncation (negative angles, values past a `Byte`/`Short`
  range), fixed-point fields (the 48.16 → wire scaling must be identical on both ends), and the
  empty/zero case. Protocol compatibility means old and new builds must agree bit-for-bit.
- Land these as colocated `_test.cpp` units — the `features/…/computation` pattern (header-pure,
  auto-globbed into engine + tests, 100% coverage per `tests/coverage.sh --auto`) — in the **same
  commit** as the adaptation.

## Verdict & verification

- Anything that needed a gate is recorded `adapted` in `commit-tracker/coverage.tsv` (note the gate),
  never `ported` raw.
- The **multiplayer E2E** (host + connect a client) is the final gate — never single-player alone,
  because a desync is invisible offline. Drive it with the `zandronum-driver` skill.
- The wire-format tests are what keep the bytes matching *after* the E2E passes; the E2E doesn't
  regress-guard the format, the golden tests do.
