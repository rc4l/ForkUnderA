/*
** gl_bsp.cpp
** Main rendering loop / BSP traversal / visibility clipping
**
**---------------------------------------------------------------------------
** Copyright 2000-2005 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
** 4. When not used as part of GZDoom or a GZDoom derivative, this code will be
**    covered by the terms of the GNU Lesser General Public License as published
**    by the Free Software Foundation; either version 2.1 of the License, or (at
**    your option) any later version.
** 5. Full disclosure of the entire project's source code, except for third
**    party libraries is mandatory. (NOTE: This clause is non-negotiable!)
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include "p_lnspec.h"
#include "p_local.h"
#include "a_sharedglobal.h"
#include "r_sky.h"
#include "p_effect.h"
#include "po_man.h"

#include "gl/renderer/gl_renderer.h"
#include "gl/data/gl_data.h"
#include "gl/data/gl_vertexbuffer.h"
#include "gl/scene/gl_clipper.h"
#include "gl/scene/gl_portal.h"
#include "gl/scene/gl_wall.h"
#include "gl/utility/gl_clock.h"
#include "features/hitboxviz/hitboxviz.h"
#include "features/levelmesh/wallcache.h"
#include "features/levelmesh/levelmesh.h"

EXTERN_CVAR(Bool, gl_render_segs)

// [rc4l] features/levelmesh P2b experiment: reuse a seg's generated wall geometry when nothing it
// was generated from has changed.
//
// Sunder MAP10: off 4.60 ms, on 4.00 ms -- ~13% faster, 98.6% hit rate, memory flat. `stat
// rendertimes` attributes it precisely: wall Setup 0.979 -> 0.228 ms.
//
// Two earlier versions of this were SLOWER, and both failures are worth remembering:
//   1. Storage grew without bound. A capture that turned out uncacheable stranded its walls, and
//      ineligible segs were re-captured every frame, so it reached 47 GB and killed the process.
//      Storage is now fixed per seg, so leaking is structurally impossible.
//   2. Validity was a content hash over ~18 fields including twelve int64 multiplies. That cost
//      about as much as the GLWall::Process it avoided, so the cache measured neutral. It is now
//      three integer compares against sector_t::fua_dirty / side_t::fua_dirty.
//
// Mode 2 is a diagnostic that skips validation entirely -- it renders stale geometry, never ship it.
namespace zx { namespace levelmesh { extern int g_wallcacheMode; } }
CUSTOM_CVAR(Int, gl_wallcache, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	zx::levelmesh::g_wallcacheMode = self;
}

Clipper clipper;


// [BB] Don't allow this in release builds.
CVAR(Bool, gl_render_things, true, CVAR_DEBUGONLY)
CVAR(Bool, gl_render_walls, true, CVAR_DEBUGONLY)
CVAR(Bool, gl_render_flats, true, CVAR_DEBUGONLY)


static void UnclipSubsector(subsector_t *sub)
{
	int count = sub->numlines;
	seg_t * seg = sub->firstline;

	while (count--)
	{
		angle_t startAngle = seg->v2->GetClipAngle();
		angle_t endAngle = seg->v1->GetClipAngle();

		// Back side, i.e. backface culling	- read: endAngle >= startAngle!
		if (startAngle-endAngle >= ANGLE_180)  
		{
			clipper.SafeRemoveClipRange(startAngle, endAngle);
		}
		seg++;
	}
}

//==========================================================================
//
// R_AddLine
// Clips the given segment
// and adds any visible pieces to the line list.
//
//==========================================================================

// making these 2 variables global instead of passing them as function parameters is faster.
static subsector_t *currentsubsector;
static sector_t *currentsector;

static void AddLine (seg_t *seg)
{
#ifdef _DEBUG
	if (seg->linedef - lines == 38)
	{
		int a = 0;
	}
#endif

	angle_t startAngle, endAngle;
	sector_t * backsector = NULL;
	sector_t bs;

	if (GLRenderer->mCurrentPortal)
	{
		int clipres = GLRenderer->mCurrentPortal->ClipSeg(seg);
		if (clipres == GLPortal::PClip_InFront) return;
	}

	startAngle = seg->v2->GetClipAngle();
	endAngle = seg->v1->GetClipAngle();

	// Back side, i.e. backface culling	- read: endAngle >= startAngle!
	// [rc4l] features/levelmesh: not during a full-level bake. Backfacing is a property of the
	// viewpoint, not of the geometry -- the wall a seg generates is the same either way, and the far
	// side of a two-sided line is a different seg that is front-facing from somewhere else in the
	// level. Culling here would bake only the half of the level facing wherever the bake happened to
	// run from. The backend draws with CULL_MODE_NONE, so winding does not matter to it.
	if (startAngle-endAngle<ANGLE_180 && !clipper.bakeAll)
	{
		return;
	}

	if (seg->sidedef == NULL)
	{
		if (!(currentsubsector->flags & SSECF_DRAWN))
		{
			if (clipper.SafeCheckRange(startAngle, endAngle)) 
			{
				currentsubsector->flags |= SSECF_DRAWN;
			}
		}
		return;
	}

	if (!clipper.SafeCheckRange(startAngle, endAngle)) 
	{
		return;
	}
	currentsubsector->flags |= SSECF_DRAWN;

	if (!seg->backsector)
	{
		clipper.SafeAddClipRange(startAngle, endAngle);
	}
	else if (!(seg->sidedef->Flags & WALLF_POLYOBJ))	// Two-sided polyobjects never obstruct the view
	{
		if (currentsector->sectornum == seg->backsector->sectornum)
		{
			FTexture * tex = TexMan(seg->sidedef->GetTexture(side_t::mid));
			if (!tex || tex->UseType==FTexture::TEX_Null) 
			{
				// nothing to do here!
				seg->linedef->validcount=validcount;
				return;
			}
			backsector=currentsector;
		}
		else
		{
			// clipping checks are only needed when the backsector is not the same as the front sector
			gl_CheckViewArea(seg->v1, seg->v2, seg->frontsector, seg->backsector);

			backsector = gl_FakeFlat(seg->backsector, &bs, true);

			if (gl_CheckClip(seg->sidedef, currentsector, backsector))
			{
				clipper.SafeAddClipRange(startAngle, endAngle);
			}
		}
	}
	else 
	{
		// Backsector for polyobj segs is always the containing sector itself
		backsector = currentsector;
	}

	seg->linedef->flags |= ML_MAPPED;

	// [rc4l] features/levelmesh: a bake pass swaps the per-linedef guard for a per-SIDEDEF one.
	// validcount would drop the second side of every two-sided line; simply bypassing it is worse,
	// because GLWall::Process spans the whole LINEDEF and every seg fragment would then emit its own
	// full-length copy -- eight coplanar walls where the BSP split a line eight ways, which renders
	// as z-fighting noise. See ClaimSideForBake in levelmesh.h.
	if ((seg->sidedef->Flags & WALLF_POLYOBJ) ||
		(clipper.bakeAll ? zx::levelmesh::ClaimSideForBake(int(seg->sidedef - sides))
		                 : (seg->linedef->validcount != validcount)))
	{
		if (!(seg->sidedef->Flags & WALLF_POLYOBJ)) seg->linedef->validcount=validcount;

		if (gl_render_walls)
		{
			SetupWall.Clock();

			// [rc4l] features/levelmesh: replay this seg's cached walls when its generation inputs
			// are unchanged, otherwise regenerate and capture the result for next frame.
			bool done = false;
			if (gl_wallcache != 0 && !(seg->sidedef->Flags & WALLF_POLYOBJ))
			{
				const int segidx = int(seg - segs);
				zx::levelmesh::WallCacheStamp stamp;
				zx::levelmesh::WallCacheEligibility elig;
				// [rc4l] Mode 2 skips stamp construction as well as the compare, so the cost of
				// BuildStamp itself is isolated. Mode 1 leaves it in. Renders stale geometry.
				if (gl_wallcache != 2)
				{
					zx::levelmesh::BuildStamp(seg, currentsector, backsector, stamp);
				}
				zx::levelmesh::BuildEligibility(seg, currentsector, backsector, elig);

				if (zx::levelmesh::TryReplay(segidx, elig, stamp))
				{
					done = true;
				}
				else
				{
					zx::levelmesh::BeginCapture(segidx);
					GLWall wall;
					wall.sub = currentsubsector;
					wall.Process(seg, currentsector, backsector);
					zx::levelmesh::EndCapture(stamp, elig);
					done = true;
				}
			}
			if (!done)
			{
				GLWall wall;
				wall.sub = currentsubsector;
				wall.Process(seg, currentsector, backsector);
			}
			rendered_lines++;

			SetupWall.Unclock();
		}
	}
}

//==========================================================================
//
// R_Subsector
// Determine floor/ceiling planes.
// Add sprites of things in sector.
// Draw one or more line segments.
//
//==========================================================================

static void PolySubsector(subsector_t * sub)
{
	int count = sub->numlines;
	seg_t * line = sub->firstline;

	while (count--)
	{
		if (line->linedef)
		{
			AddLine (line);
		}
		line++;
	}
}

//==========================================================================
//
// RenderBSPNode
// Renders all subsectors below a given node,
//  traversing subtree recursively.
// Just call with BSP root.
//
//==========================================================================

static void RenderPolyBSPNode (void *node)
{
	while (!((size_t)node & 1))  // Keep going until found a subsector
	{
		node_t *bsp = (node_t *)node;

		// Decide which side the view point is on.
		int side = R_PointOnSide(viewx, viewy, bsp);

		// Recursively divide front space (toward the viewer).
		RenderPolyBSPNode (bsp->children[side]);

		// Possibly divide back space (away from the viewer).
		side ^= 1;

		// It is not necessary to use the slower precise version here
		if (!clipper.CheckBox(bsp->bbox[side]))
		{
			return;
		}

		node = bsp->children[side];
	}
	PolySubsector ((subsector_t *)((BYTE *)node - 1));
}

//==========================================================================
//
// Unlilke the software renderer this function will only draw the walls,
// not the flats. Those are handled as a whole by the parent subsector.
//
//==========================================================================

static void AddPolyobjs(subsector_t *sub)
{
	if (sub->BSP == NULL || sub->BSP->bDirty)
	{
		sub->BuildPolyBSP();
		for (unsigned i = 0; i < sub->BSP->Segs.Size(); i++)
		{
			sub->BSP->Segs[i].Subsector = sub;
			sub->BSP->Segs[i].PartnerSeg = NULL;
		}
	}
	if (sub->BSP->Nodes.Size() == 0)
	{
		PolySubsector(&sub->BSP->Subsectors[0]);
	}
	else
	{
		RenderPolyBSPNode(&sub->BSP->Nodes.Last());
	}
}


//==========================================================================
//
//
//
//==========================================================================

static inline void AddLines(subsector_t * sub, sector_t * sector)
{
	currentsector = sector;
	currentsubsector = sub;

	ClipWall.Clock();
	if (sub->polys != NULL)
	{
		AddPolyobjs(sub);
	}
	else
	{
		int count = sub->numlines;
		seg_t * seg = sub->firstline;

		while (count--)
		{
			if (seg->linedef == NULL)
			{
				if (!(sub->flags & SSECF_DRAWN)) AddLine (seg);
			}
			else if (!(seg->sidedef->Flags & WALLF_POLYOBJ)) 
			{
				AddLine (seg);
			}
			seg++;
		}
	}
	ClipWall.Unclock();
}


//==========================================================================
//
// R_RenderThings
//
//==========================================================================

static inline void RenderThings(subsector_t * sub, sector_t * sector)
{

	SetupSprite.Clock();
	sector_t * sec=sub->sector;
	if (sec->thinglist != NULL)
	{
		// Handle all things in sector.
		for (AActor * thing = sec->thinglist; thing; thing = thing->snext)
		{
			GLRenderer->ProcessSprite(thing, sector);
			// [MGOOOOOO] Debug hitbox overlay. Collected here rather than in GLSprite::Process
			// because that path culls spriteless, +INVISIBLE and fully translucent actors, which
			// still have collision boxes worth seeing. No-op unless the overlay is on.
			zx::hitboxviz::CollectActor(thing);
		}
	}
	SetupSprite.Unclock();
}


//==========================================================================
//
// R_Subsector
// Determine floor/ceiling planes.
// Add sprites of things in sector.
// Draw one or more line segments.
//
//==========================================================================

static void DoSubsector(subsector_t * sub)
{
	unsigned int i;
	sector_t * sector;
	sector_t * fakesector;
	sector_t fake;
	
#ifdef _DEBUG
	if (sub->sector-sectors==931)
	{
		int a = 0;
	}
#endif

	sector=sub->sector;
	if (!sector) return;

	// If the mapsections differ this subsector can't possibly be visible from the current view point
	// [rc4l] features/levelmesh: a bake wants every subsector in the level, not the ones visible from
	// wherever the bake ran. This check alone was hiding well over half of Sunder MAP10 -- the mesh
	// stopped at 21312 pieces against ~26691 sides plus 2x10958 subsector planes.
	if (!clipper.bakeAll &&
		!(currentmapsection[sub->mapsection>>3] & (1 << (sub->mapsection & 7)))) return;

	if (gl_drawinfo->ss_renderflags[sub-subsectors] & SSRF_SEEN)
	{
		// This means that we have reached a subsector in a portal that has been marked 'seen'
		// from the other side of the portal. This means we must clear the clipper for the
		// range this subsector spans before going on.
		UnclipSubsector(sub);
	}

	if (GLRenderer->mCurrentPortal)
	{
		int clipres = GLRenderer->mCurrentPortal->ClipSubsector(sub);
		if (clipres == GLPortal::PClip_InFront) return;
	}



	fakesector=gl_FakeFlat(sector, &fake, false);

	if (sector->validcount != validcount)
	{
		GLRenderer->mVBO->CheckUpdate(sector);
	}

	// [RH] Add particles
	//int shade = LIGHT2SHADE((floorlightlevel + ceilinglightlevel)/2 + r_actualextralight);
	if (gl_render_things)
	{
		SetupSprite.Clock();

		for (i = ParticlesInSubsec[DWORD(sub-subsectors)]; i != NO_PARTICLE; i = Particles[i].snext)
		{
			GLRenderer->ProcessParticle(&Particles[i], fakesector);
		}
		SetupSprite.Unclock();
	}

	AddLines(sub, fakesector);

	// BSP is traversed by subsector.
	// A sector might have been split into several
	//	subsectors during BSP building.
	// Thus we check whether it was already added.
	if (sector->validcount != validcount)
	{
		// Well, now it will be done.
		sector->validcount = validcount;

		if (gl_render_things)
		{
			RenderThings(sub, fakesector);
		}
		sector->MoreFlags |= SECF_DRAWN;
	}

	if (gl_render_flats)
	{
		// Subsectors with only 2 lines cannot have any area!
		if (sub->numlines>2 || (sub->hacked&1)) 
		{
			// Exclude the case when it tries to render a sector with a heightsec
			// but undetermined heightsec state. This can only happen if the
			// subsector is obstructed but not excluded due to a large bounding box.
			// Due to the way a BSP works such a subsector can never be visible
			if (!sector->heightsec || sector->heightsec->MoreFlags & SECF_IGNOREHEIGHTSEC || in_area!=area_default)
			{
				if (sector != sub->render_sector)
				{
					sector = sub->render_sector;
					// the planes of this subsector are faked to belong to another sector
					// This means we need the heightsec parts and light info of the render sector, not the actual one.
					fakesector = gl_FakeFlat(sector, &fake, false);
				}

				BYTE &srf = gl_drawinfo->sectorrenderflags[sub->render_sector->sectornum];
				if (!(srf & SSRF_PROCESSED))
				{
					srf |= SSRF_PROCESSED;

					SetupFlat.Clock();
					GLRenderer->ProcessSector(fakesector);
					SetupFlat.Unclock();
				}
				// mark subsector as processed - but mark for rendering only if it has an actual area.
				gl_drawinfo->ss_renderflags[sub-subsectors] = 
					(sub->numlines > 2) ? SSRF_PROCESSED|SSRF_RENDERALL : SSRF_PROCESSED;
				if (sub->hacked & 1) gl_drawinfo->AddHackedSubsector(sub);

				FPortal *portal;

				portal = fakesector->portals[sector_t::ceiling];
				if (portal != NULL)
				{
					GLSectorStackPortal *glportal = portal->GetGLPortal();
					glportal->AddSubsector(sub);
				}

				portal = fakesector->portals[sector_t::floor];
				if (portal != NULL)
				{
					GLSectorStackPortal *glportal = portal->GetGLPortal();
					glportal->AddSubsector(sub);
				}
			}
		}
	}
}




//==========================================================================
//
// RenderBSPNode
// Renders all subsectors below a given node,
//  traversing subtree recursively.
// Just call with BSP root.
//
//==========================================================================

void gl_RenderBSPNode (void *node)
{
	if (numnodes == 0)
	{
		DoSubsector (subsectors);
		return;
	}
	while (!((size_t)node & 1))  // Keep going until found a subsector
	{
		node_t *bsp = (node_t *)node;

		// Decide which side the view point is on.
		int side = R_PointOnSide(viewx, viewy, bsp);

		// Recursively divide front space (toward the viewer).
		gl_RenderBSPNode (bsp->children[side]);

		// Possibly divide back space (away from the viewer).
		side ^= 1;

		// It is not necessary to use the slower precise version here
		if (!clipper.CheckBox(bsp->bbox[side]))
		{
			if (!(gl_drawinfo->no_renderflags[bsp-nodes] & SSRF_SEEN))
				return;
		}

		node = bsp->children[side];
	}
	DoSubsector ((subsector_t *)((BYTE *)node - 1));
}


