# Base-engine backport scope (#41) — the gate to full GZDoom 2.1.1

The renderer staircase landed at flight 23 (frontier `37ac6ef9a`). Everything
beyond it toward `g2.1.1` is blocked because, in the 2.1.1 codebase, renderer
features are woven into three ZDoom-2015 base-engine changes Zandronum's 2012
fork never took. This is the scope of unblocking them. It is a **playsim
effort, not a renderer flight** — sim-side, netcode-touching, fixed64-critical.

## The three pieces, in dependency order

### 1. Coordinate accessor refactor — the dominant cost and the gate

ZDoom migrated actor position from public fields (`actor->x/y/z`) to accessor
methods (`actor->X()/Y()/Z()`, `SetXYZ`, `AddZ`, …) plus value types
`fixedvec2`/`fixedvec3` and relative helpers (`Vec3To`, `Vec3Offset`,
`Vec3Angle`, `InterpolatedPosition`, `GetBobOffset`, `SetOrigin(fixedvec3, moving)`).
This was the groundwork for eventual float coordinates.

- **Migration surface (measured in our tree): ~1,939 `actor->x/y/z` access
  sites** across the engine. Every one converts to an accessor call.
- **fixed64 is central**: every converted site crosses the 64-bit 48.16 strong
  type. `fixedvec3` must carry `zx::Fixed`, and each accessor/helper is a fixed64
  boundary — this is precisely what the `fixed64-widening` skill exists to guard,
  applied ~2000 times. The strong type will catch mistakes at compile time, which
  is an asset here, but the volume is large.
- **Netcode implications**: actor positions replicate through `sv_commands.cpp`
  (307 hand-written SERVERCOMMANDS). Position writes/reads must keep the wire
  format identical (48.16 fixed) while the in-memory API changes — the wire model
  in `features/fixed64/computation/net_fixed_wire_compute` is the reference.
- **Approach**: mechanical, auditable, done as its OWN migration (not mixed with
  features). Introduce the accessors as thin inline wrappers over the existing
  fields first (zero behavior change, compiles identically), then migrate call
  sites in batches with the fixed64 audit per batch, then flip the fields private
  last. Each batch is independently testable.

### 2. Line portals + Eternity portals

Playsim portal subsystems: `Line_SetPortal` linedef special, the portal
linedef/sector plumbing, portal traversal (movement, hitscan, sound), and the
GL-side `GLEEHorizonPortal` / stacked-sector-portal render paths. Netcode must
handle actors traversing portals (position/velocity across the seam). Depends on
(1) for the coordinate API. Medium effort; net-new gameplay features.

### 3. Skybox type system + the deferred 3D-light-splitting renderer piece

`SKYBOX_MAP` skybox type detection, and then the flight-24 renderer work that was
deferred here: `GLWall::SplitWall` → hardware-clip 3D lights, `CopyFrom3DLight`,
per-region wall/sprite/decal light clipping (upstream `fc57180d7`..`c129cb3ca`).
Once (1) and (2) exist, these apply on top — the staircase can resume from
`37ac6ef9a` and run to `g2.1.1`. The verification instrument already exists:
`progress/renderer-staircase/testmaps/` (3D-floor + dynamic-light map with a
flight-23 baseline to A/B against).

## Recommended sequencing

1. **Coordinate refactor first**, alone, as an auditable batch migration (the
   gate; ~2000 fixed64 sites + netcode parity). Nothing else is applicable until
   this lands.
2. **Line/Eternity portals** on top (gameplay features + their netcode).
3. **Resume the staircase** from `37ac6ef9a`: skybox types, 3D-light-splitting,
   final fixes → `g2.1.1`. The ledger rows currently marked `skipped (#41)` /
   `pending` flip to `ported`/`adapted` as each lands.

## Why this wasn't a renderer flight

The staircase's discipline was "one renderer at all times, verbatim replay." At
`37ac6ef9a` the replay hits files where a single commit changes both a light-clip
plane and an `X()` accessor call — the renderer and the sim stop being separable.
That is the correct place to stop the renderer track and start the base-engine
track. See the staircase ledger for the exact per-commit disposition.
