// [rc4l] Pure decision for what a railgun's trace-endpoint spawns, extracted from P_RailAttack so it
// is unit-testable without the engine. Historically the rail only spawned its endpoint puff/decal on
// a WALL hit; this widens the puff to floor and ceiling hits too, and the mark with it, which features/decals
// places or refuses as it does a shot's. On a floor/ceiling hit the caller must also apply the sky-flat guard --
// destroy the just-spawned puff if it landed on a sky flat (unless MF3_SKYEXPLODE) -- so a rail into
// the open sky leaves no floating puff. The TRACE_Hit* values are mirrored here and static_asserted
// in the caller. Implementation in rail_puff_compute.cpp.
#ifndef ZX_RAIL_PUFF_COMPUTE_H
#define ZX_RAIL_PUFF_COMPUTE_H

// [rc4l] Mirrors enum ETraceStatus in p_trace.h.
enum
{
	ZX_TRACE_HitNone    = 0,
	ZX_TRACE_HitFloor   = 1,
	ZX_TRACE_HitCeiling = 2,
	ZX_TRACE_HitWall    = 3,
	ZX_TRACE_HitActor   = 4,
};

// [rc4l] What the rail endpoint should spawn for a given trace hit type.
struct RailEndpointSpawn
{
	bool puff;     // spawn the endpoint puff (subject to the caller's ALWAYSPUFF check)
	bool decal;    // mark the surface hit, through features/decals
	bool skyGuard; // floor/ceiling hit: caller must destroy the puff if it's on a sky flat
};

// [rc4l] Endpoint puff and mark on a wall, floor or ceiling hit.
RailEndpointSpawn ComputeRailEndpointSpawn(int traceHitType);

#endif // ZX_RAIL_PUFF_COMPUTE_H
