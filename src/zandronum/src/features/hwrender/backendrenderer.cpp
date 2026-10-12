// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The renderer of a launch without GL: the engine's view through features/frame, and the level, light and fake-flat work GL's interface did on the CPU.

#include "gl/system/gl_system.h"
#include "features/hwrender/backendonly.h"
#include "features/hwrender/hud2d.h"           // [rc4l] RenderCameraTexture
#include "features/frame/playerview.h"
#include "features/viewsetup/viewsetup.h"   // [rc4l] InstallSpriteView

#include "c_cvars.h"
#include "d_player.h"
#include "m_png.h"
#include "r_renderer.h"
#include "r_defs.h"
#include "g_level.h"
#include "farchive.h"
#include "textures/textures.h"

// [rc4l] GL's CPU-side level, light and texture code, which needs no context.
#include "gl/gl_functions.h"               // gl_PreprocessLevel, gl_CleanLevelData, gl_SetActorLights
#include "gl/data/gl_data.h"               // gl_InitPortals
#include "gl/renderer/gl_renderer.h"       // gl_FakeFlat
#include "gl/renderer/gl_lightdata.h"      // gl_SetFogParams, the attached lights
#include "gl/textures/gl_material.h"
#include "gl/textures/gl_hwtexture.h"      // GetTexDimension

void gl_ParseDefs();
EXTERN_CVAR(Float, maxviewpitch)
EXTERN_CVAR(Bool, cl_disallowfullpitch)
EXTERN_CVAR(Bool, cl_oldfreelooklimit)

namespace {

struct FBackendRenderer : public FRenderer
{
	bool UsesColormap() const { return false; }
	// [rc4l] Textures go to the backend as it first draws them, so there is nothing to warm.
	void PrecacheTexture(FTexture *, int) {}

	void RenderView(player_t *player) { zx::frame::RenderPlayerView(player, zx::hwrender::BackendPaletteBrightness()); }

	// [rc4l] No picture of the view yet: the save is written with the placeholder the software path writes when it has none.
	void WriteSavePic(player_t *, FILE *file, int, int) { M_CreateDummyPNG(file); }

	void StateChanged(AActor *actor) { gl_SetActorLights(actor); }
	void StartSerialize(FArchive &) { gl_DeleteAllAttachedLights(); }
	void EndSerialize(FArchive &arc)
	{
		gl_RecreateAllAttachedLights();
		if (arc.IsLoading()) gl_InitPortals();
	}

	// [rc4l] A camera texture is the backend's own view into its own target, as GL's RenderTextureView hands it over.
	void RenderTextureView(FCanvasTexture *tex, AActor *viewpoint, int fov)
	{
		FMaterial *mat = FMaterial::ValidateTexture(tex, false);
		if (mat != NULL && viewpoint != NULL)
		{
			const int width = mat->TextureWidth(), height = mat->TextureHeight();
			zx::hwrender::RenderCameraTexture(mat, FHardwareTexture::GetTexDimension(mat->GetWidth()), FHardwareTexture::GetTexDimension(mat->GetHeight()),
				(float)width / height, (int)viewpoint->x, (int)viewpoint->y, (int)(viewpoint->z + (viewpoint->height >> 1)),
				(unsigned int)viewpoint->angle, 0, (float)fov);
		}
		tex->SetUpdated();
	}

	sector_t *FakeFlat(sector_t *sec, sector_t *tempsec, int *floorlightlevel, int *ceilinglightlevel, bool back)
	{
		if (floorlightlevel != NULL) *floorlightlevel = sec->GetFloorLight();
		if (ceilinglightlevel != NULL) *ceilinglightlevel = sec->GetCeilingLight();
		return gl_FakeFlat(sec, tempsec, back);
	}

	void SetFogParams(int fogdensity, PalEntry outsidefogcolor, int outsidefogdensity, int skyfog)
	{
		gl_SetFogParams(fogdensity, outsidefogcolor, outsidefogdensity, skyfog);
	}
	void PreprocessLevel() { gl_PreprocessLevel(); }
	void CleanLevelData() { gl_CleanLevelData(); }
	bool RequireGLNodes() { return true; }

	// [rc4l] [BB]/[AK] Zandronum's pitch limits, as GL's interface gives them.
	int GetMaxViewPitch(bool down)
	{
		if (cl_disallowfullpitch || zadmflags & ZADF_FORCE_SOFTWARE_PITCH_LIMITS) return down ? 56 : (cl_oldfreelooklimit ? 32 : 56);
		return maxviewpitch;
	}
	void ClearBuffer(int) {}
	// [rc4l] GLDEFS, and the sprite derivation answered from features/viewsetup as GL's interface answers it from itself.
	void Init()
	{
		gl_ParseDefs();
		zx::viewsetup::InstallSpriteView();
	}
};

} // namespace

namespace zx { namespace hwrender {

FRenderer *CreateBackendRenderer() { return new FBackendRenderer; }

}} // namespace zx::hwrender
