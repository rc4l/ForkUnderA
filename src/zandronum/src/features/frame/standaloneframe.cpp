// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/standaloneframe.h"
#include "features/frame/computation/actorcull_compute.h"

#include "features/levelmesh/staticmesh.h"     // [rc4l] DynClear
#include "features/levelmesh/flatmesh.h"       // [rc4l] PlayerWalkActive, ClearSprites, RecordSpriteSweep
#include "features/levelmesh/projdecals.h"     // [rc4l] UpdateProjectedDecals
#include "features/levelmesh/wallcache.h"      // [rc4l] InvalidateMovedSectors
#include "features/horizon/horizon.h"
#include "features/visibility/viswalk.h"
#include "features/hwrender/hud2d.h"           // [rc4l] SceneMVP
#include "features/sprites/spritebuild.h"

#include "actor.h"
#include "c_cvars.h"
#include "dthinker.h"
#include "p_effect.h"
#include "po_man.h"
#include "r_defs.h"
#include "r_state.h"   // [rc4l] subsectors

#include <vector>

// [rc4l] Standalone walks the player's view without GL for its sky surfaces, the automap and the cull, as GL's walk serves GL; about 0.04 ms on wdiex02.
CVAR(Bool, fua_vis_walk, true, 0)
// [rc4l] Off walks without culling, so the walk's own cost and the cull's can be measured apart.
CVAR(Bool, fua_vis_cull, true, 0)
EXTERN_CVAR(Bool, fua_sprites_own)
EXTERN_CVAR(Bool, fua_sprite_check)
EXTERN_CVAR(Bool, fua_shading_check)
EXTERN_CVAR(Bool, fua_surface_mapbake_auto)
EXTERN_CVAR(Bool, gl_render_things)

namespace zx { namespace frame {

namespace {

const SceneHooks *g_hooks = NULL;

// [rc4l] The particles in a subsector a walk reached, derived, or handed back when the backend leaves them.
void WalkParticles(subsector_t *sub, sector_t *sector)
{
	if (!gl_render_things) return;
	for (WORD i = ParticlesInSubsec[DWORD(sub - subsectors)]; i != NO_PARTICLE; i = Particles[i].snext)
		if (!(fua_sprites_own || fua_sprite_check) || !zx::sprites::BuildParticle(&Particles[i], sector))
			if (g_hooks->particle != NULL) g_hooks->particle(&Particles[i], sector);
}

// [rc4l] An actor a stacked view's or mirror's walk reached, its sprite or model derived inside that view.
void PortalActor(AActor *thing)
{
	if (fua_sprites_own) zx::sprites::BuildSprite(thing);
}

// [rc4l] The actors the sweep keeps, reused frame to frame.
std::vector<AActor *> g_swept;

// [rc4l] The actors the frustum may hold, derived, or handed back when the backend leaves them; taken from the thinker list, which is where they live, rather than found by a walk.
void SweepActors()
{
	int seen = 0, noSector = 0, behind = 0, offScreen = 0, processed = 0;
	// [rc4l] Derived in one batch across the job pool unless a check compares them one at a time against GL.
	const bool batch = fua_sprites_own && !fua_sprite_check && !fua_shading_check;
	g_swept.clear();
	const float *mvp = zx::hwrender::SceneMVP();
	TThinkerIterator<AActor> it;
	AActor *thing;
	while ((thing = it.Next()) != NULL)
	{
		seen++;
		if (thing->Sector == NULL) { noSector++; continue; }
		if (mvp != NULL)
		{
			// [rc4l] Mesh space is (x, up, y), and the actor's size pads the test since a sprite is wider than the point it stands on.
			const float pad = FIXED2FLOAT(thing->radius) + FIXED2FLOAT(thing->height) + 64.f;
			const int cull = ComputeActorCull(mvp, FIXED2FLOAT(thing->x), FIXED2FLOAT(thing->z) + FIXED2FLOAT(thing->height) * 0.5f,
				FIXED2FLOAT(thing->y), pad);
			if (cull == kActorBehind) { behind++; continue; }
			if (cull == kActorOffScreen) { offScreen++; continue; }
		}
		processed++;
		if (batch) g_swept.push_back(thing);
		else if (!(fua_sprites_own || fua_sprite_check) || !zx::sprites::BuildSprite(thing))
			if (g_hooks->actor != NULL) g_hooks->actor(thing);
	}
	if (!g_swept.empty()) zx::sprites::BuildSprites(&g_swept[0], (int)g_swept.size());
	zx::levelmesh::RecordSpriteSweep(seen, noSector, behind, offScreen, processed);
}

} // namespace

bool StandaloneScene(const SceneHooks &hooks)
{
	g_hooks = &hooks;
	// [rc4l] Last frame's sprites are dead the moment the camera moves, marks age on the frame clock, and a sector that moved has invalidated its cached geometry.
	if (zx::levelmesh::PlayerWalkActive())
	{
		zx::levelmesh::DynClear();
		zx::levelmesh::ClearSprites();
		zx::levelmesh::UpdateProjectedDecals();
	}
	zx::horizon::UpdateHorizons();
	// [rc4l] The walks first, so a sector or side that moved or relit is re-baked only once one of them reaches it.
	const bool walked = fua_vis_walk && zx::levelmesh::PlayerWalkActive();
	if (walked)
	{
		PO_LinkToSubsectors();
		zx::visibility::WalkView(fua_vis_cull, true, WalkParticles);
		zx::visibility::WalkPortalViews(PortalActor);
	}
	zx::levelmesh::WalkedThisFrame walkedSet;
	walkedSet.sectors = zx::visibility::SectorsWalkedThisFrame(walkedSet.sectorCount);
	walkedSet.sides = zx::visibility::SidesWalkedThisFrame(walkedSet.sideCount);
	zx::levelmesh::InvalidateMovedSectors(walked ? &walkedSet : NULL);
	// [rc4l] A moved sector re-bakes from the map when the map drives the bake; otherwise the caller's own derivation re-bakes it.
	if (zx::levelmesh::SectorsMovedLastFrame() != 0 && !fua_surface_mapbake_auto) return false;
	SweepActors();
	return true;
}

}} // namespace zx::frame
