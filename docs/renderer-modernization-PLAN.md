# Renderer modernization — the program

**Status: active. This supersedes the renderer task set, not the levelmesh plan it builds on.**

Sibling documents, and what stays true in them:

- [`levelmesh-PLAN.md`](levelmesh-PLAN.md) — the persistent-mesh work. Its findings (topology is
  immutable at runtime, plane movement has a clean chokepoint, polyobjects are rigid bodies) are the
  foundation this program stands on. Phases 2 and 3 here are its Phase 3 and 4, restated with the
  measurements we now have.
- [`renderer-staircase.md`](renderer-staircase.md) — replaying GZDoom 2014–2016. Orthogonal and
  independently resumable; gated on the base-engine backport (#41).

## The policy that changes everything

**GL is the oracle, not the specification.**

Until now every port decision was settled by "does it match GL". That was correct while the backend
was unproven: a pixel-identical claim is testable, and "looks about right" is not. It is no longer
the goal. Where GL is a faithful expression of Doom, we match it. Where GL is a 2004 workaround for
hardware that no longer exists, **we are allowed to be better**, and the test becomes a stated
property rather than a diff against GL.

Concretely, three things must not change, because changing them changes the game:

- **Doom's lighting semantics** — sector light levels, `R_DoomLightingEquation`, fake contrast,
  colormaps, fog-as-light, `LIGHTLEVEL`/`GLDEFS` and the mod-facing definitions.
- **Doom's translucency semantics** — the render styles, `STYLEF_*` flags, and what a mod means when
  it declares one.
- **Mod-facing surfaces** — DECALDEF, GLDEFS, dynamic light defs, ANIMDEFS, ZScript/DECORATE
  expectations.

Everything else — how a frame is built, in what order, on which processor — is ours.

## Why now, and in this order

The measured baseline (Sunder MAP10, RTX 5080, 640x400): **4.0 ms CPU render, 1.74 ms GPU, 8251
walls/frame.** The GPU idles ~70% of the render window. We are CPU-bound in the renderer by ~2.3x,
and every optimization we have added — the wall cache, batching, the mesh — is a bet against the one
assumption the entire renderer rests on: *everything is rebuilt every frame, so nothing can go
stale.*

That bet is currently placed silently, in struct fields, with nothing standing behind it. It has been
called wrong at least once per optimization:

| optimization | the assumption it broke | what it cost |
|---|---|---|
| wall cache | a light index cannot outlive its frame | a dead plasma bolt lit a wall for 400 tics; ~1 day |
| wall batching | a wall's state is set immediately before its draw | batched walls got no dynamic light at all |
| wall cache | a material cannot change under a cached wall | LAVFALL1..4 was a still image in GL |
| deferred decals | a texture binding is per-draw | only slot 0 of the array ever bound; ~1 day |

So the ordering below is not "cheapest first". It is **retire the mechanism that generates the bug
family, then take the performance it was blocking.**

## Phase 0 — make frame lifetime a compile-time property

**The rule:** a value that describes *a frame* may not be stored in a structure that outlives one.

- `FrameScoped<T>` — resets on copy, so a cached struct cannot carry one in by accident.
- `GLWall`, `GLFlat` and every mesh-cached record audited field by field: *property of the world* or
  *property of the frame*.
- A `static_assert` on the size of each cached record, so adding a field forces the classification
  instead of inheriting one.

**Accepts when:** reintroducing the stale-light-index bug is a compile error, demonstrated by a
deliberate attempt. Zero runtime cost; zero pixels change.

**Buys:** the entire bug family above, retired. This is the cheapest phase and the reason the rest
can move fast.

## Phase 1 — clustered lighting

Retires: `side_t::lighthead`, `subsector_t::lighthead`, `FLightNode`, `LinkLight`'s mark-and-sweep,
`GLWall::SetupLights`, `FLightBuffer`'s per-surface index ranges, and the `GLPASS_LIGHTSONLY` pass.

The Diligent backend has already dropped per-surface lists — it uploads every active light to one
storage buffer and tests them per fragment. That was written for correctness (a baked mesh cannot
hold a per-frame index) and it is precisely the shape clustering wants. What is missing is the grid:
a froxel volume over the view, each cell holding the indices of the lights that touch it, so a
fragment tests ~4 lights instead of ~400.

- Cluster assignment and light→cluster binning as a computation unit, with tests.
- CPU binning first (correct, measurable), compute-shader binning second (fast).
- GL keeps its own machinery until Phase 4 retires the path.

**Accepts when:** 1000 dynamic lights on dbab04 hold frame time within 10% of 0 lights, and light
appearance matches GL within tolerance for the counts GL can actually render.

**Buys:** dynamic lights stop being a scarce resource — and every light edge case (dormant,
on-plane, zero-radius, orphaned node) loses the machinery it lived in. Prerequisite for shadows.

## Phase 2 — GPU-driven geometry

Retires: the BSP walk as an object factory, per-frame `GLWall` construction, per-frame vertex
streaming for static geometry.

- Persistent GPU-resident mesh for everything (walls done, flats partly, sprites not).
- Per-draw data in a storage buffer indexed by draw ID; materials bindless or array-indexed.
- GPU frustum + occlusion culling producing an indirect draw buffer.
- Multi-threaded scene extraction for what remains on the CPU.

**Accepts when:** Sunder MAP10 CPU render time is under 1.5 ms and the frame is GPU-bound. That is
the ~2.3x, and it is the phase that actually moves fps.

## Phase 2b — one surface, not two

Retires: the wall/flat split.

Doom's software renderer drew walls as vertical columns and floors as horizontal spans — two
completely different inner loops, because that is what made 1993 hardware fast. The GL renderer
inherited the *shape* of that split without the reason: `GLWall` and `GLFlat`, separate draw lists,
separate sorting, separate light setup, separate caches, separate batching. Every feature is
therefore written twice, and the two halves drift.

That drift is not theoretical — it has been the direct cause of at least three faults:

- Batched walls got no dynamic light at all while flats were fine, because `DrawFlats` still draws
  one at a time and refreshes each flat's light index; a torch lit the floor under it and left the
  wall behind it dark.
- The stale light index could only ever affect walls, for the same reason.
- Projected decals had to be taught floors and ceilings separately from walls (`fua_decal_flats`),
  and each side broke the other twice.

A surface is a surface: some geometry, a plane, a material, a light level, a colormap. Nothing about
a floor needs a different type from a wall once the renderer is neither span-based nor column-based.

- One `Surface` record, one mesh, one draw list, one sort key, one light path.
- `GLWall`/`GLFlat` become thin builders that emit surfaces, then disappear.
- Slopes stop being a special case: a sloped floor is a plane like any other.

**Accepts when:** no feature in the renderer is implemented twice, and the decal, light and ordering
suites pass with a single code path serving walls, floors and ceilings.

## Phase 3 — one ordering authority

Retires: implicit draw order as a correctness mechanism.

Today a translucent surface's appearance depends on BSP order, drawlist bucketing, per-list sorts and
a comparator — five sources, no authority. `decalorder_compute` exists and only the port uses it.

- One sort key, computed by one computation unit, used by both renderers.
- Decals fully projected; the glued-quad path deleted.
- Sprites and particles instanced, sorted by the same key.

**Accepts when:** every ordering decision in the frame comes from that one function, and the decal
suite passes on both renderers with the glued path compiled out.

## Phase 4 — shadows

- Shadow maps for dynamic lights (impossible today: a per-surface light list has nowhere to put a
  visibility query), then RT shadows on the BVH the mirrors already maintain.

**GL stays.** Retiring it was in this phase and has been taken out deliberately: it is the only
independent check on everything above, and every phase here is measured by rendering the same frozen
frame both ways. A renderer with no second opinion is one where a plausible-looking mistake has
nothing to run into. It goes when it is costing more than it is telling us, and that is a decision to
make with the evidence of the phases in hand, not now.

## How every phase is measured

Same map, same spawn, same resolution, `fuactl bench` / `perf-ab` rather than a single capture; and
for anything visual, a scenario in `tools/fuactl/scenarios/` that answers with a number.

Two rules, both bought with days:

1. **Never measure a region containing the thing under test.** A "blue tint" number that averaged
   over the scorch marks was reporting decal coverage; it moved exactly like the fault and was
   wrong. `png --tint` exists because of it.
2. **Never accept a null result from one run of an intermittent fault.** The stale-light bug
   reproduces in about half of runs and was called unreproducible twice.

## Tracking

Umbrella issue plus one issue per phase. Superseded renderer items are folded in by reference, not
closed — see the umbrella for the map.
