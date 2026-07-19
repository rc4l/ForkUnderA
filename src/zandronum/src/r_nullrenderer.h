// [rc4l] Null renderer for the GL-only build.
//
// The software renderer (FSoftwareRenderer) has been removed to shed the
// BUILD-engine licensed code, so the engine renders exclusively through the
// OpenGL FRenderer implementation (gl_CreateInterface()). The dedicated server
// (NO_GL) is built without OpenGL and never renders anything, but it still needs
// *a* concrete FRenderer. This header-only class provides trivial no-op
// implementations of the pure virtuals so the server links and runs.

#ifndef __R_NULLRENDERER_H
#define __R_NULLRENDERER_H

#include "r_renderer.h"

struct FNullRenderer : public FRenderer
{
	bool UsesColormap() const { return false; }
	void PrecacheTexture(FTexture *tex, int cache) {}
	void RenderView(player_t *player) {}
	void WriteSavePic(player_t *player, FILE *file, int width, int height) {}
	int GetMaxViewPitch(bool down) { return 56; }
	void ClearBuffer(int color) {}
	void Init() {}
	void RenderTextureView(FCanvasTexture *tex, AActor *viewpoint, int fov) {}
	sector_t *FakeFlat(sector_t *sec, sector_t *tempsec, int *floorlightlevel, int *ceilinglightlevel, bool back)
	{
		return sec;
	}
};

#endif
