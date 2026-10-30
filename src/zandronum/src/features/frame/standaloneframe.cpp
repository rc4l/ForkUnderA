// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/standaloneframe.h"
#include "features/frame/computation/actorcull_compute.h"
#include "features/frame/computation/cameraseen_compute.h"

#include "features/levelmesh/staticmesh.h"     // [rc4l] DynClear
#include "features/hitboxviz/hitboxviz.h"         // [rc4l] CollectBackendFrame
#include "features/levelmesh/flatmesh.h"       // [rc4l] PlayerWalkActive, ClearSprites, RecordSpriteSweep
#include "features/levelmesh/wallcache.h"      // [rc4l] InvalidateMovedSectors
#include "features/framephase/framephase.h"     // [rc4l] the walk, rebake and actor phases
#include "features/levelmesh/walldecals.h"     // [rc4l] BuildWallDecals
#include "features/horizon/horizon.h"
#include "features/visibility/viswalk.h"
#include "features/hwrender/backendonly.h"
#include "features/sprites/spritebuild.h"
#include "features/gamecopy/actors.h"        // [rc4l] the actor records the sweep reads
#include "features/gamecopy/particles.h"     // [rc4l] the particles the walks reach
#include "features/gamecopy/players.h"       // [rc4l] the camera textures the copy holds
#include "features/jobs/jobs.h"                // [Doruk-Aksoy] the lanes the sweep culls on
#include "features/telemetry/telemetry.h"      // [Doruk-Aksoy] scene/actor_sweep

#include "actor.h"
#include "c_cvars.h"
#include "dthinker.h"
#include "p_effect.h"
#include "po_man.h"
#include "r_defs.h"
#include "r_state.h"   // [rc4l] subsectors
#include "textures/textures.h"

#include <vector>

// [rc4l] Standalone walks the player's view without GL for its sky surfaces, the automap and the cull, as GL's walk serves GL; about 0.04 ms on wdiex02.
CVAR(Bool, fua_vis_walk, true, 0)
// [rc4l] Off walks without culling, so the walk's own cost and the cull's can be measured apart.
CVAR(Bool, fua_vis_cull, true, 0)
// [Doruk-Aksoy] Off culls the actors on the main thread alone, to bisect or compare in one session.
CVAR(Bool, fua_vis_sweepjobs, true, 0)
EXTERN_CVAR(Bool, fua_sprite_check)
EXTERN_CVAR(Bool, fua_shading_check)
EXTERN_CVAR(Bool, gl_render_things)

namespace zx { namespace frame {

namespace {

// [rc4l] The particles in a subsector a walk reached, derived.
void WalkParticles(subsector_t *sub, sector_t *sector)
{
	if (!gl_render_things) return;
	for (WORD i = zx::gamecopy::FirstParticleIn(int(sub - subsectors)); i != NO_PARTICLE; i = zx::gamecopy::CopiedParticle(i)->snext)
		zx::sprites::BuildParticle(zx::gamecopy::CopiedParticle(i), zx::gamecopy::ParticleIdentity(i), sector);
}

// [rc4l] An actor a stacked view's or mirror's walk reached, its sprite or model derived inside that view.
void PortalActor(const zx::gamecopy::ActorRecord &thing)
{
	zx::sprites::BuildSprite(thing);
}

// [rc4l] The actors the sweep keeps, reused frame to frame.
std::vector<const zx::gamecopy::ActorRecord *> g_swept;

// [Doruk-Aksoy] What the sweep makes of an actor: kept, or why it is left out.
enum SweepVerdict { kSweepKept, kSweepNoSector, kSweepBehind, kSweepOffScreen, kSweepVerdicts };

SweepVerdict VerdictOf(const zx::gamecopy::ActorRecord *thing, const float *mvp)
{
	if (thing->Sector == NULL) return kSweepNoSector;
	if (mvp == NULL) return kSweepKept;
	// [rc4l] Mesh space is (x, up, y), and the actor's size pads the test since a sprite is wider than the point it stands on.
	const float pad = FIXED2FLOAT(thing->radius) + FIXED2FLOAT(thing->height) + 64.f;
	const int cull = ComputeActorCull(mvp, FIXED2FLOAT(thing->x), FIXED2FLOAT(thing->z) + FIXED2FLOAT(thing->height) * 0.5f,
		FIXED2FLOAT(thing->y), pad);
	return cull == kActorBehind ? kSweepBehind : cull == kActorOffScreen ? kSweepOffScreen : kSweepKept;
}

// [Doruk-Aksoy] One lane's run of the actors: those it kept, in record order, and how many got each verdict.
struct SweepChunk
{
	std::vector<const zx::gamecopy::ActorRecord *> kept;
	int verdicts[kSweepVerdicts];
};
std::vector<SweepChunk> g_sweepChunks;
// [Doruk-Aksoy] Actors a lane culls at a time.
const int kSweepChunk = 512;

// [rc4l] The actors the frustum may hold, derived; taken from the thinker list, which is where they live, rather than found by a walk.
void SweepActors()
{
	int verdicts[kSweepVerdicts] = {};
	// [rc4l] Derived in one batch across the job pool unless a check compares them one at a time against GL.
	const bool batch = !fua_sprite_check && !fua_shading_check;
	g_swept.clear();
	const float *mvp = zx::hwrender::SceneMVP();
	int count;
	const zx::gamecopy::ActorRecord *records = zx::gamecopy::Actors(count);
	{
		ZX_ZONE("scene/actor_sweep");   // [Doruk-Aksoy] the actors culled against the view, before any is derived
		if (batch && fua_vis_sweepjobs && count > 0)
		{
			// [Doruk-Aksoy] Chunks of the records culled on the lanes, their kept lists joined in chunk order so the batch is in record order as one pass made it.
			const int chunks = zx::jobs::ComputeChunkCount(count, kSweepChunk);
			if ((int)g_sweepChunks.size() < chunks) g_sweepChunks.resize(chunks);
			auto sweep = [&](int lo, int hi, int)
			{
				SweepChunk &sc = g_sweepChunks[lo / kSweepChunk];
				sc.kept.clear();
				for (int &v : sc.verdicts) v = 0;
				for (int i = lo; i < hi; i++)
				{
					const SweepVerdict v = VerdictOf(&records[i], mvp);
					sc.verdicts[v]++;
					if (v == kSweepKept) sc.kept.push_back(&records[i]);
				}
			};
			zx::jobs::Pool().ParallelChunks(count, kSweepChunk, sweep);
			for (int c = 0; c < chunks; c++)
			{
				const SweepChunk &sc = g_sweepChunks[c];
				g_swept.insert(g_swept.end(), sc.kept.begin(), sc.kept.end());
				for (int v = 0; v < kSweepVerdicts; v++) verdicts[v] += sc.verdicts[v];
			}
		}
		else
			for (int i = 0; i < count; i++)
			{
				const SweepVerdict v = VerdictOf(&records[i], mvp);
				verdicts[v]++;
				if (v != kSweepKept) continue;
				if (batch) g_swept.push_back(&records[i]);
				else zx::sprites::BuildSprite(records[i]);
			}
	}
	if (!g_swept.empty()) zx::sprites::BuildSprites(&g_swept[0], (int)g_swept.size());
	zx::levelmesh::RecordSpriteSweep(count, verdicts[kSweepNoSector], verdicts[kSweepBehind], verdicts[kSweepOffScreen], verdicts[kSweepKept]);
}

// [rc4l] The camera textures on what the player's walk saw, drawn again next frame, as GL's binding one marks it; the rest keep their last picture, as GL's do.
void MarkSeenCameraTextures(const zx::levelmesh::WalkedThisFrame &walked)
{
	static std::vector<FCanvasTexture *> canvases;
	static std::vector<int> cameras, textures;
	static std::vector<unsigned char> seen;
	canvases.clear();
	cameras.clear();
	// [rc4l] features/gamecopy: the cameras and the map's textures as the copy holds them, since ACS SetCameraToTexture and a switch change the live ones from a tic beside this frame.
	for (const zx::gamecopy::CameraTexture &c : zx::gamecopy::CameraTextures())
		if (c.texture != NULL) { canvases.push_back(c.texture); cameras.push_back(c.picnum.GetIndex()); }
	if (cameras.empty()) return;
	textures.clear();
	for (int i = 0; i < walked.sideCount; i++)
		for (int part = side_t::top; part <= side_t::bottom; part++) textures.push_back(sides[walked.sides[i]].rc.GetTexture(part).GetIndex());
	for (int i = 0; i < walked.sectorCount; i++)
	{
		textures.push_back(sectors[walked.sectors[i]].rc.GetTexture(sector_t::floor).GetIndex());
		textures.push_back(sectors[walked.sectors[i]].rc.GetTexture(sector_t::ceiling).GetIndex());
	}
	ComputeCamerasSeen(cameras, textures, seen);
	for (size_t i = 0; i < canvases.size(); i++)
		if (seen[i]) canvases[i]->NeedUpdate();
}

} // namespace

void StandaloneScene()
{
	if (!zx::hwrender::StandaloneActive()) return;
	const zx::levelmesh::SceneScope scene;
	// [rc4l] Last frame's sprites are dead the moment the camera moves, marks age on the frame clock, and a sector that moved has invalidated its cached geometry.
	if (zx::levelmesh::PlayerWalkActive())
	{
		zx::levelmesh::DynClear();
		zx::levelmesh::ClearSprites();
	}
	zx::horizon::UpdateHorizons();
	// [rc4l] The walks first, so a sector or side that moved or relit is re-baked only once one of them reaches it.
	const bool walked = fua_vis_walk && zx::levelmesh::PlayerWalkActive();
	if (walked)
	{
		const zx::visibility::WalkHooks hooks = { WalkParticles, PortalActor, zx::levelmesh::BuildWallDecals };
		const zx::framephase::Scope walkPhase(zx::framephase::kPhaseWalk);
		zx::visibility::WalkView(fua_vis_cull, true, &hooks);
		zx::visibility::WalkPortalViews(&hooks);
	}
	// [rc4l] A camera texture's view walks for its sky surfaces alone, so another sky than the level's shows on its screen.
	else if (fua_vis_walk) zx::visibility::WalkCameraSky();
	zx::levelmesh::WalkedThisFrame walkedSet;
	walkedSet.sectors = zx::visibility::SectorsWalkedThisFrame(walkedSet.sectorCount);
	walkedSet.sides = zx::visibility::SidesWalkedThisFrame(walkedSet.sideCount);
	{
		const zx::framephase::Scope rebakePhase(zx::framephase::kPhaseRebake);
		zx::levelmesh::InvalidateMovedSectors(walked ? &walkedSet : NULL);
	}
	if (walked) MarkSeenCameraTextures(walkedSet);
	// [rc4l] The hitbox view's boxes for the player's view, from the sectors its walk reached as GL's RenderThings takes them.
#ifndef NO_GL
	if (zx::levelmesh::PlayerWalkActive()) zx::hitboxviz::CollectBackendFrame(camera, walkedSet.sectors, walkedSet.sectorCount);
#endif
	const zx::framephase::Scope actorsPhase(zx::framephase::kPhaseActors);
	SweepActors();
}

}} // namespace zx::frame
