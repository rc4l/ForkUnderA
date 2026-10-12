// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The backend's entry points for a build without it (FUA_DILIGENT off, every non-Windows build): each answers that there is no backend, so GL carries every frame.

#include "features/hwrender/backendonly.h"
#include "features/hwrender/hud2d.h"

#include "c_cvars.h"
#include "c_dispatch.h"
#include "c_console.h"

// [rc4l] Read by the standalone frame and the -vulkan launch, which neither runs here.
CVAR(Bool, fua_surface_mapbake_auto, true, 0)

CCMD( fua_diligent_scene ) { Printf( "This build has no Diligent backend (-DFUA_DILIGENT=ON).\n" ); }
CCMD( fua_diligent_bench ) { Printf( "This build has no Diligent backend (-DFUA_DILIGENT=ON).\n" ); }
CCMD( fua_diligent_scale ) { Printf( "This build has no Diligent backend (-DFUA_DILIGENT=ON).\n" ); }

class DFrameBuffer;

namespace zx { namespace hwrender {

bool BackendPresenting() { return false; }
bool StandaloneActive() { return false; }
const float *SceneMVP() { return NULL; }
void LiveFrame() {}
void SetMainViewLens(float, float) {}
void RenderCameraTexture(const void *, int, int, float, fixed_t, fixed_t, fixed_t, unsigned int, int, float) {}

void GetDeferredDecalStats(int &boxes, int &draws, int &textures, const char **bail)
{
	boxes = draws = textures = 0;
	*bail = "no backend in this build";
}

DFrameBuffer *CreateBackendFrameBuffer(void *, int, int, int, int, bool) { return NULL; }
int BackendPaletteBrightness() { return 0; }
bool OpenBackendOutput(int, int) { return false; }
int BackendMaxTextureSize() { return 0; }
void NoteWorldView() {}
void PresentBackendOnly(bool) {}
unsigned char *ReadBackendFrame(int &, int &) { return NULL; }

}} // namespace zx::hwrender
