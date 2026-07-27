# ZScript → ZandroX logic: feasibility study (2026-07-23)

**Question:** can ZScript logic be converted to run in ZandroX, given that running ZScript itself is
understood to be incompatible with our netcode?

## 1. The incompatibility, precisely (measured in our tree)

The commonly cited reasons (float math, VM) are secondary. The primary wall is **the replication
model**:

- ZandroX/Zandronum is client/server. The server replicates game state through **307 distinct
  hand-written `SERVERCOMMANDS_*` messages** used across **121 files** (measured). Every scriptable
  action is *individually* network-instrumented — `thingdef_codeptr.cpp` (the DECORATE action
  functions) alone contains 61 explicit replication calls.
- Client prediction (`cl_pred.cpp`, `CLIENT_PREDICT_*`) predicts a **known, closed set** of
  behaviors.
- ZScript's entire point is **open-ended** logic: arbitrary per-tick code, arbitrary state mutation.
  The server cannot auto-replicate what it cannot enumerate, and clients cannot predict it. The
  alternatives are full state-delta sync (the "extremely inflated bandwidth" the Zandronum tracker
  cites) or lockstep — i.e., **a new netcode**.
- Aggravators: ZScript semantics are double-based (post-2016 float sim) — cross-platform float
  divergence poisons demos/prediction, and retrofitting fixed-point under double semantics silently
  changes mod behavior; plus the VM/GC would run inside the sim tick.

**Verdict on running ZScript: infeasible without redesigning the netcode.** This matches the
Zandronum developers' own position ([tracker 3106](https://zandronum.com/tracker/view.php?id=3106)).

## 2. Conversion (transpilation) — the feasible path

Converting ZScript **source** into DECORATE + ACS (+ targeted engine extensions) sidesteps the wall
entirely, because **the output lands on constructs that are already hand-instrumented for the
netcode** — converted logic inherits replication and prediction for free.

Why the mapping is real, not hopeful:

- **ZScript is a superset that grew out of DECORATE.** Since GZDoom 3.0, DECORATE is internally
  compiled onto the same VM as ZScript — the equivalence of the shared subset is upstream-proven;
  a converter walks the well-defined direction backwards for that subset.
- **Actor classes** (properties, flags, states, standard action functions) → DECORATE mechanically.
- **Imperative methods with scalar math** → ACS, which is integer/16.16-fixed — *native* to our
  fixed-point sim heritage — and fully networked in Zandronum (ACS is the supported multiplayer
  scripting today).

### The convertible subset (mechanical)
Actor definitions; state machines; inventory/weapon logic built on standard codepointers; A_*
action calls; simple expressions/conditionals; sounds/spawns/damage.

### Convertible with effort (tool + human/LLM assist)
Custom functions → ACS functions; simple Tick() overrides → looping ACS scripts on the actor's TID;
vector math → fixed-point ACS equivalents (precision-audited).

### Not convertible (needs redesign or engine extension)
Non-actor classes and interfaces; dynamic arrays/maps; arbitrary virtual overrides
(`ModifyDamage`, UI event handlers); ZScript menus/statusbars/HUDs (stay C++/SBARINFO/MENUDEF);
readonly-pointer iterators; anything depending on double-precision behavior.

## 3. Recommended program

1. **No ZScript VM in ZandroX.** Settled by §1.
2. **Prototype the transpiler** against a corpus of popular ZScript mods; measure the convertible
   fraction empirically (static translation for the mechanical share, LLM-assisted for idioms, and
   a hard report of unconvertible constructs per mod).
3. **Grow the target, demand-driven:** where the corpus shows a recurring unconvertible idiom, add
   a DECORATE property/codepointer or ACS function for it — each hand-instrumented for the netcode,
   exactly how Zandronum has always extended safely. Every addition permanently widens the
   convertible subset.

**Bottom line:** running ZScript = a netcode rewrite (infeasible as scoped); *converting* ZScript =
feasible for a meaningful and growable subset, with netcode safety inherited by construction from
the DECORATE/ACS targets.

Sources: measurements from this tree (`sv_commands.h`, `thingdef_codeptr.cpp`, `cl_pred.cpp`);
[Zandronum tracker 3106](https://zandronum.com/tracker/view.php?id=3106);
[Doomworld discussion](https://www.doomworld.com/forum/topic/113341-do-you-think-zandronum-should-merge-with-glzdoom-at-some-point/);
[Zandronum ACS wiki](https://zdoom.org/wiki/Zandronum:_ACS).
