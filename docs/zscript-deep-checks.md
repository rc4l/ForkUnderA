# ZScript × netcode: the C++ deep checks (2026-07-23)

Every mechanism we could think of, checked against the actual code — ours and upstream's — rather
than argued in the abstract. Companion to `docs/zscript-feasibility.md` and
`docs/zscript-insulation.md`.

## The measured facts (what the code says)

| Check | Result | Where |
|---|---|---|
| How ACS runs in netplay | **Server-side VM; effects replicate through instrumented natives** — 203 network gates in the ACS interpreter; `CLIENTSIDE` scripts are an explicit separate class | `p_acs.cpp` |
| Do script-defined fields replicate? | **No — and everything works anyway.** `SetUserVariable` writes raw actor memory with zero replication; user variables are *server-private state* by design | `p_acs.cpp:5576` |
| Instrumented property surface | **42 distinct `APROP_*` actor properties** (the setters that DO replicate) | `p_acs.cpp` |
| GC write barrier as a store hook? | No — `GC::WriteBarrier` covers **object-pointer writes only** (GC bookkeeping, 2008-era), not a general field hook | `dobject.h:285` |
| Client prediction scope | **Local player only** — `cl_pred.cpp` re-simulates `P_PlayerThink` for the console player; no other actor is ever predicted | `cl_pred.cpp` |
| VM stack size to port | **~41,700 lines** (vm 5.5k + core 9k + backend 17.4k + frontend 7.4k + interface 2.5k) *before* its object-system dependencies (PClass/PType/serialization) | UZDoom `src/common/scripting` |
| C++ language level | Engine is C++14; the modern scripting stack wants C++20 — **isolatable per-file** (pattern proven on this branch for the backend) — language is NOT the blocker | build system |

## The key reframe these checks produce

The incompatibility is **narrower than "arbitrary writes."** In the ACS model that Zandronum already
runs: arbitrary *script-private* state needs **no replication at all** (user variables prove it in
production); only writes to **engine-simulated fields** (position, health, velocity…) must go
through the instrumented surface, because that is all clients ever see. And prediction risk is
confined to scripts that modify the **local player's movement** — nothing else is predicted.

## The mechanism matrix

| # | Mechanism | Verdict from the checks |
|---|---|---|
| 1 | VM store-barrier + generic field sync | No free hook exists (GC barrier is pointer-only); bandwidth ceiling unbounded; **dominated by #4** |
| 2 | Snapshot/delta netcode | A netcode rewrite, not a trick — out |
| 3 | Deterministic client-side VM (lockstep-style) | Architecture mismatch: Zandronum clients don't simulate other actors at all (measured) — out |
| 4 | **"ZScript on the ACS model"** — server-side VM; script fields server-private; engine-field stores **compile-lowered onto the instrumented property surface** (42 APROP, widened demand-driven); `CLIENTSIDE` class for HUD | **The one honest C++ architecture that fits the existing netcode.** Bandwidth is semantic (same class as ACS today). Cost: ~50–60k lines ported (VM + object-system deps, C++20-isolated) + a store-lowering compiler pass + native instrumentation. A GZDoom-3.x-object-system-sized project — feasible, not cheap, not a trick |
| 5 | **Transpiler ZScript → DECORATE/ACS** | **Same runtime model as #4 at a fraction of the cost** — converted code lands on the identical instrumented surface, and `SetUserVariable` gives converted scripts real per-actor storage *today*. Loses only the unconvertible subset |
| 6 | X-macro/codegen auto-instrumentation of natives | Cost reducer for widening the native surface — worth building under either #4 or #5 |
| 7 | Client-side-only VM (UI/HUD) | Possible (no replication needed) but ~40k lines for cosmetics our C++ MENUDEF/SBARINFO already covers — not worth it alone |

## Verdict

The deep checks **upgrade** both prior conclusions:

- There **is** one C++ architecture (#4) that hosts real ZScript syntax under the existing netcode
  without a rewrite — server-side execution, server-private script state, store-lowering onto the
  instrumented surface. It is a ~50–60k-line project, kept on the shelf as the long-term option if
  mod-ecosystem pressure ever justifies it.
- The transpiler path (#5) is *stronger* than previously scoped: it runs on the same model, the
  per-actor storage it needs already exists and already behaves correctly un-replicated, the
  property surface it must target is enumerated (42, extendable), and its prediction risk is
  precisely bounded (local-player movement mods only).

**Order of operations stands: transpiler first; #4 shelved but real; the tripwire guards the engine
either way.**
