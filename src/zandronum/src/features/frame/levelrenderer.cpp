// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/frame/levelrenderer.h"
#include "features/hwrender/backendonly.h"
#include "features/viewsetup/computation/viewsetup_compute.h"  // [rc4l] ComputeMaxViewPitch
#include "features/levelmesh/wallcache.h"                      // [rc4l] AllocForLevel, FreeLevel
#include "features/gamecopy/gamecopy.h"                         // [rc4l] InitLevel
#include "features/gamecopy/actors.h"                           // [rc4l] the camera texture's viewpoint record
#include "features/levelmesh/staticmesh.h"                     // [rc4l] MeshInitForLevel, MeshFreeLevel
#include "features/visibility/viswalk.h"                       // [rc4l] ForgetWalk
#include "r_state.h"                                           // [rc4l] numsegs

#include "c_cvars.h"
#include "doomstat.h"   // zadmflags
#include "farchive.h"
#include "r_defs.h"

#include "gl/gl_functions.h"               // [rc4l] gl_PreprocessLevel, gl_CleanLevelData: GL's own data for a level
#include "features/levelprep/levelprep.h"
#include "features/gldefs/gldefs.h"
#include "features/stackportals/stackportals.h"   // [rc4l] gl_InitPortals
#include "features/levelmesh/flatmesh.h"   // [rc4l] SetStackViewCapacity
#include "features/fakeflat/fakeflat.h"   // [rc4l] gl_FakeFlat
#include "features/rendersettings/rendersettings.h"   // [rc4l] gl_SetFogParams
#include "features/materials/materials.h"

CUSTOM_CVAR(Float, maxviewpitch, 90.f, CVAR_ARCHIVE|CVAR_SERVERINFO)
{
	if (self>90.f) self=90.f;
	else if (self<-90.f) self=-90.f;
}
EXTERN_CVAR(Bool, cl_disallowfullpitch)
EXTERN_CVAR(Bool, cl_oldfreelooklimit)

namespace zx { namespace frame {

// [rc4l] Neither renderer draws through the software colormaps, so they are never built.
bool FLevelRenderer::UsesColormap() const { return false; }

void FLevelRenderer::StateChanged(AActor *actor) { gl_SetActorLights(actor); }

void FLevelRenderer::StartSerialize(FArchive &) { gl_DeleteAllAttachedLights(); }

void FLevelRenderer::EndSerialize(FArchive &arc)
{
	gl_RecreateAllAttachedLights();
	if (arc.IsLoading())
	{
		gl_InitPortals();
		zx::levelmesh::SetStackViewCapacity(StackPortalsInUse());
	}
}

sector_t *FLevelRenderer::FakeFlat(sector_t *sec, sector_t *tempsec, int *floorlightlevel, int *ceilinglightlevel, bool back)
{
	if (floorlightlevel != NULL) *floorlightlevel = sec->GetFloorLight();
	if (ceilinglightlevel != NULL) *ceilinglightlevel = sec->GetCeilingLight();
	return gl_FakeFlat(sec, tempsec, back);
}

void FLevelRenderer::SetFogParams(int fogdensity, PalEntry outsidefogcolor, int outsidefogdensity, int skyfog)
{
	gl_SetFogParams(fogdensity, outsidefogcolor, outsidefogdensity, skyfog);
}

// [rc4l] The level's shared data, then GL's, then the backend's mesh and per-seg bake state for it.
void FLevelRenderer::PreprocessLevel()
{
	zx::levelprep::PrepareLevel();
	gl_PreprocessLevel();
	zx::gamecopy::InitLevel();
	zx::levelmesh::AllocForLevel(numsegs);
	zx::levelmesh::MeshInitForLevel();
}

// [rc4l] The backend's mesh, bake state and walks go first, while the level they point into is still whole.
void FLevelRenderer::CleanLevelData()
{
	zx::levelmesh::FreeLevel();
	zx::visibility::ForgetWalk();
	zx::levelmesh::MeshFreeLevel();
	gl_CleanLevelData();
	zx::levelprep::CleanLevel();
}
bool FLevelRenderer::RequireGLNodes() { return true; }

int FLevelRenderer::GetMaxViewPitch(bool down)
{
	// [rc4l] [BB] Zandronum clamps the pitch differently; [AK] ZADF_FORCE_SOFTWARE_PITCH_LIMITS forces the software limits.
	return zx::viewsetup::ComputeMaxViewPitch(down, cl_disallowfullpitch || (zadmflags & ZADF_FORCE_SOFTWARE_PITCH_LIMITS),
		cl_oldfreelooklimit, (int)maxviewpitch);
}

void FLevelRenderer::Init() { gl_ParseDefs(); }

void RenderBackendCamera(zx::materials::MaterialRef mat, int record, int fov)
{
	const zx::gamecopy::ActorRecord *const viewpoint = zx::gamecopy::Record(record);
	if (mat == NULL || viewpoint == NULL) return;
	int width, height, storedW, storedH;
	zx::materials::MaterialRenderSize(mat, width, height);
	zx::materials::MaterialStoredSize(mat, storedW, storedH);
	zx::hwrender::RenderCameraTexture(mat, storedW, storedH,
		(float)width / height, viewpoint->x, viewpoint->y, viewpoint->z + (viewpoint->height >> 1), (unsigned int)viewpoint->angle, 0, (float)fov);
}

}} // namespace zx::frame
