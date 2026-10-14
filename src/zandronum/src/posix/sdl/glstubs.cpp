#include "r_defs.h"
#include "r_state.h"
#include "v_video.h"

#include "features/materials/textureinfo.h"

#ifdef NO_GL
CVAR (Float, vid_brightness, 0.f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR (Float, vid_contrast, 1.f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)

void FTexture::UncacheGL()
{
}

void gl_CleanLevelData()
{
}

// [rc4l] g_level.cpp forward-declares this and calls it from G_SerializeLevel unconditionally, so a
// NO_GL build needs the symbol even though it has nothing to serialize. Every other gl_* entry point
// reached from shared code is stubbed here for the same reason; this one was simply missed, and only
// a SERVERONLY link would have said so.
class FArchive;
void gl_SerializeGlobals(FArchive &)
{
}

void gl_PreprocessLevel()
{
}

void StartGLMenu (void)
{
}

// [rc4l] Takes the cache layer that c74fcda (staircase flight 17) added to the declaration. The stub
// kept the old nullary signature for weeks because NO_GL is only reached by a SERVERONLY build and
// nothing in CI made one.
void FTexture::PrecacheGL(int cache)
{
}

// [rc4l] gl_ParseDefs, AddStateLight and MiscGLInfo are features/gldefs and features/materials now, which a build without GL compiles too.
// [rc4l] The two light settings whose callbacks reset GL's shader state stay in gl/, so a build without GL defines them plainly.
CVAR(Bool, r_enhanced_nightvision, true, CVAR_ARCHIVE|CVAR_NOINITCALL)
// [rc4l] And the texture filter's two, whose callbacks reset GL's samplers; the backend rebuilds its pipelines when they change.
CVAR(Float, r_texture_filter_anisotropic, 1.0f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG|CVAR_NOINITCALL)
CVAR(Int, r_texture_filter, 0, CVAR_ARCHIVE|CVAR_GLOBALCONFIG|CVAR_NOINITCALL)
CVAR(Int, r_lightmode, 3, CVAR_ARCHIVE|CVAR_NOINITCALL)
// [rc4l] A build without GL has no GL frame buffer, so only the backend's draws the screen.
class DFrameBuffer;
DFrameBuffer *I_CreateGLFrameBuffer(int, int, bool)
{
	return NULL;
}

#endif
