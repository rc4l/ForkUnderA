// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The half of the renderer interface that is the level, its lights and its fog rather than drawing, shared by GL's renderer and the one a launch without GL draws with.

#ifndef ZX_LEVELRENDERER_H
#define ZX_LEVELRENDERER_H

#include "v_palette.h"   // [rc4l] PalEntry, which r_renderer.h uses without including
#include "r_renderer.h"
#include "features/materials/materials.h"   // [rc4l] MaterialRef


namespace zx { namespace frame {

// [rc4l] Each renderer derives from this and overrides only what it draws with: the view, the save picture, camera textures, precaching and clearing.
struct FLevelRenderer : public FRenderer
{
	bool UsesColormap() const;
	void StateChanged(AActor *actor);
	void StartSerialize(FArchive &arc);
	void EndSerialize(FArchive &arc);
	sector_t *FakeFlat(sector_t *sec, sector_t *tempsec, int *floorlightlevel, int *ceilinglightlevel, bool back);
	void SetFogParams(int fogdensity, PalEntry outsidefogcolor, int outsidefogdensity, int skyfog);
	void PreprocessLevel();
	void CleanLevelData();
	bool RequireGLNodes();
	int GetMaxViewPitch(bool down);
	void Init();
};

// [rc4l] A camera texture's view handed to the backend, which draws it into its own target whichever renderer drew the screen.
void RenderBackendCamera(zx::materials::MaterialRef mat, AActor *viewpoint, int fov);

}} // namespace zx::frame

#endif // ZX_LEVELRENDERER_H
