#include "r_defs.h"
#include "r_state.h"
#include "v_video.h"

#include "gl/textures/gl_texture.h"

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

void gl_ParseDefs()
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

FTexture::MiscGLInfo::MiscGLInfo() throw ()
{
}

FTexture::MiscGLInfo::~MiscGLInfo()
{
}

void AddStateLight(FState *, const char *)
{
}

#endif
