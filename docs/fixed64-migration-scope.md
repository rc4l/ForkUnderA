# Scoping: widening `fixed_t` to 64-bit

Goal: giant maps and high-precision slopes **without** switching the simulation to
floating point, so lockstep netcode determinism is preserved. The lever is a wider
*integer* fixed-point type, not `double`.

This document scopes the work. It is a map, not a plan of record.

## Why not float

Zandronum is cross-platform lockstep multiplayer (x86 + ARM clients, a server). Every
machine must compute bit-identical simulation results. Integer math is bit-identical
everywhere; IEEE floating point is not (x87 vs SSE, FMA contraction, `-O2` reordering,
per-libm transcendentals, x86 vs ARM). GZDoom could floatify its sim because it is
single-player-first with no lockstep to protect. We cannot. A 64-bit integer fixed-point
type gives the same range and precision as `double` while staying deterministic.

`fixed_t` today: `typedef SDWORD fixed_t` (signed 32-bit), 16.16 format
(`basictypes.h:91,94`). Range ±32768 map units, precision 1/65536.

Target: a 64-bit signed fixed-point type. Two shapes are possible:
- **48.16** (keep `FRACBITS=16`, widen the integer part): range ±140 billion units,
  precision unchanged. In-range arithmetic stays bit-identical to today. Smallest blast
  radius; preserves the most existing behaviour.
- **40.24** (more fractional bits): more precision, but changes rounding for every
  existing value. Larger blast radius.

**48.16 is the recommended shape** — it delivers the range for giant maps, keeps precision,
and minimises behavioural divergence. Extra fractional precision for slopes can come later
if measured to be needed.

## The finding that reshapes the goal

**Widening `fixed_t` alone does NOT give you giant maps.** The classic Doom/Hexen binary
map format stores coordinates as 16-bit `short`:

- `doomdata.h` — `mapvertex_t { short x, y }`, `mapnode_t`, `mapthing_t`
- loaded and shifted `<< FRACBITS` in `p_setup.cpp:938,1156,1588,1692,1881`

So vertices are capped at ±32768 *on load*, before they ever become a `fixed_t`. The wider
integer would sit half-empty for any classic map.

**Giant maps require UDMF** — the text map format (`p_udmf.cpp`, already present) whose
coordinates are arbitrary-precision text. The real deliverable is three things together:

1. widen `fixed_t` (engine capacity), **and**
2. a UDMF ingest path that does not clamp coordinates to 16-bit, **and**
3. the range-assumption fixes below.

The binary format stays ±32k forever — that is its spec, and it is how GZDoom works too.

## Determinism stays intact throughout

Everything here is integer. Widening does not introduce float into the sim. The renderer
still converts fixed→float at draw time as it always has. No determinism property changes.

---

## Work surface

### 1. The math layer — the chokepoint

All scale primitives live in one clean-room file, `computation/fixedmath.h` (~135 inline
functions): `Scale`, `MulScale`, `DivScale`, `DMulScale`, `TMulScale`, `BoundMulScale`, and
the unrolled `MulScale1..32` / `DivScale1..32` / `DMulScale1..32` / `TMulScale1..32`
families. `FixedMul` ≡ `MulScale16`, `FixedDiv` ≡ `SafeDivScale16`
(`m_fixed.h:65-66`); the overflow-clamped `SafeDivScale1..32` block is `m_fixed.h:22-63`.

Every one uses an `int64_t` intermediate and returns `int32_t`:
`FixedMul(a,b) = (int64_t)a*b >> 16`.

At 64-bit, `a*b` overflows 64 bits → the intermediate must become **128-bit**.

- **clang (macOS, Linux): `__int128` works — confirmed.** These platforms are free.
- **MSVC (Windows): no `__int128`.** Multiply-scale needs `_umul128` + `__shiftright128`;
  the hard primitive is the **128÷64 divide** (`DivScale`/`Scale`/`SafeDivScale`) — `_div128`
  is x64-only and absent on ARM64 MSVC, so a **software 128/64 long-division fallback** is
  required as the universal path.

`FIXED_MAX`/`FIXED_MIN` (`basictypes.h:97-98`) are the 32-bit clamp bounds baked into
`SafeDivScale`; widen them together or the clamps corrupt 64-bit results. `xs_Float.h`
(`FLOAT2FIXED`) is also 32-bit-hardcoded and needs rerouting.

The file is standalone (`#include <cstdint>` only) and fully unit-tested
(`computation/fixedmath_test.cpp`, reference-formula comparison). The natural rewrite is
**templated on value + intermediate type**, re-deriving the tests against a 128-bit
reference. This is the `Compute*` pattern and is the correct place to start — self-contained
and testable without the engine.

### 2. Coordinate storage — dozens of fields, mechanical

~35–45 coordinate-bearing `fixed_t` fields, concentrated in:
- `actor.h` — `x,y,z` (`:976`), `floorz/ceilingz/dropoffz`, `radius,height`,
  `velx,vely,velz`, `SpawnPoint[3]`, `PrevX/Y/Z`, `lastX/Y/Z` (unlagged), speeds/step heights
- `r_defs.h` — `vertex_t.x,y` (`:90`), `secplane_t.d` (`:250`, distance term),
  `line_t.dx,dy` + `bbox` (`:1063,1073`), `node_t.x,y,dx,dy` + child bboxes (`:1205`),
  sector `soundorg/vboheight/TexZ`
- `p_local.h` — `divline_t.x,y,dx,dy`, `FLineOpening`, `FBoundingBox` (`m_bbox.h:86`)

Non-coordinate `fixed_t` (alpha, scale, friction, texture offsets) — roughly the other
half of all `fixed_t` fields — do **not** need widening. Widen coordinates, leave the rest.

### 3. Range assumptions — ~150–200 sites

- `FIXED_MAX`/`FIXED_MIN` used as coordinate sentinels: `ONFLOORZ`/`ONCEILINGZ`/`FLOATRANDZ`
  (`p_local.h:123-125`) equal the 32-bit extremes; every `z == ONFLOORZ` compare must be
  audited when the bit pattern changes.
- `R_PointToAngle2` fast-path guard `|x|,|y| < INT_MAX/4` (`r_utility.cpp:204`).
- Blockmap: `0x1FF` 512-block wraparound and `bmaporgx/y` math (`p_setup.cpp:3183`,
  `p_local.h:50-80`); `GetSafeBlockX/Y` already has partial 64-bit overloads.
- ~60 `0x80000000` sign-bit tricks — the load-bearing one is the BSP side test
  `R_PointOnSideSlow` (`p_maputl.cpp:478,480`), hardcoded bit 31.
- ~320 `>> FRACBITS` / `>> 16` coordinate→`int` truncations: the `& 0xffff` frac-mask
  sites stay correct; the ones storing into `int`/`short` lose the widened high bits — those
  are the hazard and need individual review.

### 4. Netcode — generated code, plus a missing 64-bit primitive

The server→client protocol is **generated** from `protocolspec/spec.*.txt` by
`protocolspec/generator/`. Do not hand-edit `network/servercommands.cpp`.

Two fixed-point wire types (`protocolspec/generator/parametertypes.py`):

| Spec type | Encoding | Wire | Impact |
|---|---|---|---|
| `Fixed` (`:358`) | `addLong` / `ReadLong` | **32-bit** | truncates a 64-bit coord — must widen |
| `AproxFixed` (`:371`) | `addShort(v >> FRACBITS)` | **16-bit whole units, ±32767** | a permanent range cap on every coord it carries |

`BYTESTREAM_s` has **no 64-bit primitive** — `WriteLong/ReadLong` are hard 32-bit
(`networkshared.cpp:257,425`), and `NetCommand::addInteger` takes an `int`
(`netcommand.cpp:145`). Steps:

1. Add `WriteLong64`/`ReadLong64` to `BYTESTREAM_s` and a 64-bit `addLong64`/`addInteger`.
2. Change `FixedParameter` (and `AngleParameter`) in the generator to emit them; **regenerate**.
3. **Decide `AproxFixed` policy.** Many coordinate sends use it (e.g. `MovePlayer` z/vel,
   many SpawnThing/Move fields). While it stays 16-bit whole-unit, those coordinates are
   capped at ±32767 *on the wire* regardless of `fixed_t` width — so on a giant map, an actor
   past ±32k transmits a wrong position. Getting giant maps over the network means promoting
   the coordinate `AproxFixed` fields to a wider encoding, not just widening `Fixed`.

### 5. ACS — a permanent 32-bit boundary (do not widen; shim)

ACS is a 32-bit integer VM by spec; its `fixed` is a hardcoded 16.16 contract independent of
engine `FRACBITS` (`p_acs.cpp:4269` uses a literal `/65536.f`). Compiled bytecode carries
32-bit values, so ACS can never widen.

**UZDoom is the exact precedent.** Even with a `double` engine, UZDoom keeps ACS 16.16 and
converts at the boundary (`p_acs.cpp:597-609`):

```cpp
inline double ACSToDouble(int acsval) { return acsval / 65536.; }
inline int    DoubleToACS(double val) { return FloatToFixed<16>(val); }
```

We do the same with 64-bit fixed as the internal type: add `ACSToFixed64` / `Fixed64ToACS`
shims. `GetActorX/Y/Z` (`p_acs.cpp:11824`) and `SetActorPosition` (`:11801`) are the seams.

**Permanent, inherited constraint:** because the ACS value is 32-bit 16.16, coordinates
beyond ±32768 units are not expressible to old scripts — `GetActorX` wraps. UZDoom has this
too and routes around it via ZScript float functions. The engine gets giant maps; ACS
coordinate reads stay ±32k-limited. Not a blocker; a known ceiling.

### 6. Savegames — auto-widen if the alias is exact

`fixed_t` has no dedicated `FArchive` operator; it resolves through the integer path. A
64-bit `SQWORD`/`QWORD` operator already exists (`farchive.h:199`, `farchive.cpp:942`). If
`fixed_t` is typedef'd to exactly `SQWORD`, `arc << coord` routes to the 8-byte operator for
free. **Audit for coordinates serialized via an explicit `(DWORD&)`/`int` temp** — those
truncate. Key sites: `AActor::Serialize` (`p_mobj.cpp:209-243`), `FMapThing::Serialize`
(`:427`), plane/sector geometry (`p_saveg.cpp:382,453`).

### 7. Demos — inherit the netcode contract

Zandronum client demos (`cl_demo.cpp`) record the raw server→client byte stream, so
regenerating the protocol updates the demo body automatically. One hand-written 32-bit site:
`CLD_LCMD_WARPCHEAT` (`cl_demo.cpp:808,576`). The `d_protocol.cpp` ticcmd path carries no
fixed_t positions (inputs are `short`) — low priority.

### What NOT to touch

- `angle_t` (`tables.h:98`) — separate 32-bit BAM type, unrelated to 16.16. Do not widen.
- `GLfixed` (`gl/api/glext.h`) — external OpenGL ES ABI.
- `dsfixed_t` (`basictypes.h:95`) — software span-drawer texel type; texture-space, likely
  dead under the GL-only direction. Decide separately.

---

## Magnitude

- Files mentioning `fixed_t`: **244**; ~2,700 lines.
- Coordinate fields to widen: **~35–45** (mechanical).
- Range-assumption sites: **~150–200**.
- `>>FRACBITS`→int truncations to audit: **~320**.
- Scale-math API: **~135** functions, feeding thousands of call sites (hundreds needing thought).

This is a **hundreds-of-sites, multi-phase change**, not a typedef swap. Determinism is
preserved by construction. Backward compatibility with pre-change content (old demos, old
servers, classic-format map ranges) is intentionally dropped.

## Suggested phasing

Each phase ends green (build + tests) and is independently reviewable.

0. **Math layer.** Rewrite `computation/fixedmath.h` + `m_fixed.h` `SafeDivScale` to a
   128-bit intermediate, templated on width, with the MSVC software 128/64 divide. Re-derive
   `fixedmath_test.cpp` against a 128-bit reference. Self-contained; no engine link needed.
1. **Retype + compile.** `fixed_t → SQWORD`; widen `FIXED_MAX/MIN`; fix struct fields and
   the `ONFLOORZ`-style sentinels. Get it building.
2. **Audit.** The ~320 `>>FRACBITS`→int truncations and ~60 `0x80000000` sign hacks.
3. **Netcode.** `BYTESTREAM_s` 64-bit primitive + generator (`FixedParameter`) + the
   `AproxFixed` coordinate policy; regenerate `servercommands.cpp`.
4. **ACS shim.** `ACSToFixed64`/`Fixed64ToACS` at the `GetActor*`/`SetActorPosition` seams.
5. **UDMF giant-map ingest.** The path that actually delivers giant maps.
6. **Savegame audit.** Confirm the `SQWORD` alias carries coordinates without truncation.

Phase 0 is the foundation and the safest place to start — it is pure, tested computation and
the rest of the engine follows its return-width contract.
