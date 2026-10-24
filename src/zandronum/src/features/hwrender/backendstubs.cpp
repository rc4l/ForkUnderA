// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The backend's entry points for a build without it (FUA_DILIGENT off, every non-Windows build): each answers that there is no backend, so GL carries every frame.

#include "features/hwrender/backendonly.h"

#include "c_cvars.h"
#include "c_dispatch.h"
#include "c_console.h"

CCMD( fua_diligent_scene ) { Printf( "This build has no Diligent backend (-DFUA_DILIGENT=ON).\n" ); }
CCMD( fua_diligent_bench ) { Printf( "This build has no Diligent backend (-DFUA_DILIGENT=ON).\n" ); }
CCMD( fua_diligent_scale ) { Printf( "This build has no Diligent backend (-DFUA_DILIGENT=ON).\n" ); }
CCMD( fua_projdecals_stats ) { Printf( "This build has no Diligent backend (-DFUA_DILIGENT=ON).\n" ); }

class DFrameBuffer;

namespace zx { namespace hwrender {

bool BackendPresenting() { return false; }
bool BackendDeviceStarts() { return false; }
zx::frame::FrameRoute FrameRouteNow(bool hasGL, bool hasBackend)
{
	const zx::frame::FrameRouteIn in = { hasGL, hasBackend, false, false, false, true, true, false };
	return zx::frame::ComputeFrameRoute(in);
}
bool StandaloneActive() { return false; }
const float *SceneMVP() { return NULL; }
void LiveFrame() {}
void RenderCameraTexture(const void *, int, int, float, fixed_t, fixed_t, fixed_t, unsigned int, int, float) {}

DFrameBuffer *CreateBackendFrameBuffer(void *, int, int, int, int, bool) { return NULL; }
int BackendPaletteBrightness() { return 0; }
bool OpenBackendOutput(int, int) { return false; }
void ForgetBackendWindow(void *) {}
int BackendMaxTextureSize() { return 0; }
const char *BackendApiName() { return ""; }
void NoteWorldView() {}
void ForgetBackendTextures() {}
void SettleLevelScene() {}
void QueueMaterialPrecache(const void *, int) {}
void PresentBackendOnly(bool) {}
void PrintDiligentMessages() {}
unsigned char *ReadBackendFrame(int &, int &) { return NULL; }
unsigned char *RenderSavePicture(int, int, float) { return NULL; }
bool RenderSavePictureLater(int, int, float, std::function<void(const unsigned char *, int, int)>) { return false; }

}} // namespace zx::hwrender
