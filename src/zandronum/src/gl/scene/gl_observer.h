#ifndef __GL_OBSERVER_H
#define __GL_OBSERVER_H

// [rc4l] What GL reports of its own walk and draw to whoever checks another renderer against it.
//
// None of it is needed to draw: GL calls these only while gl_observer is set, which features/sidebyside
// does while a check that compares against GL is on (fua_vis_check, fua_sprite_check, fua_psprite_check).
// GL owns this interface, so GL depends on no renderer module to be watched.

#include "features/draw2d/computation/quad2d_compute.h"   // [rc4l] Quad2D, the weapon quad GL draws

class AActor;
class FMaterial;
class FModelVertexBuffer;
class VSMatrix;
class GLWall;
class GLSprite;
struct line_t;

struct FGLObserver
{
	virtual ~FGLObserver() {}
	// [rc4l] A scene drawn, outermost or nested inside a portal, and a BSP walk starting in it.
	virtual void EnterScene() {}
	virtual void LeaveScene() {}
	virtual void WalkBegins() {}
	// [rc4l] The walk reached a subsector, or saw a sidedef.
	virtual void WalkedSubsector(int subsector) {}
	virtual void WalkedSide(int side) {}
	// [rc4l] A wall GL made a sky quad of (with the skybox it names, NULL for a plain sky), a sky hole, a stacked view's wall, or a mirror's sheen.
	virtual void SkyQuad(const GLWall &wall, AActor *skybox) {}
	virtual void SkyHole(const GLWall &wall) {}
	virtual void StackWall(const GLWall &wall, const void *portal) {}
	virtual void MirrorSheen(const GLWall &wall) {}
	// [rc4l] A walk from a skybox's viewpoint, through a stacked view, or into a mirror, begun and ended.
	virtual void SkyboxWalkBegins() {}
	virtual void StackWalkBegins(const void *portal) {}
	virtual void MirrorWalkBegins(int line) {}
	virtual void PortalWalkEnds() {}
	// [rc4l] The player's walk is done while GL's draw info still holds it, and so are the portal views it opened.
	virtual void PlayerWalkDone() {}
	virtual void PortalViewsDone() {}
	// [rc4l] A sprite GL drew, by its four corners; a model's surfaces between ModelBegins and ModelEnds; the weapon's quad.
	virtual void Sprite(const GLSprite &sprite, const float *c1, const float *c2, const float *c3, const float *c4) {}
	virtual void ModelBegins(const GLSprite *sprite, const VSMatrix &model) {}
	virtual void ModelSurface(FModelVertexBuffer *vbuf, unsigned int base1, unsigned int base2, float inter,
		unsigned int firstIndex, unsigned int count, FMaterial *tex, int translation) {}
	virtual void ModelEnds() {}
	virtual void WeaponQuad(const zx::hwrender::Quad2D &q) {}
};

// [rc4l] The observer GL reports to, NULL while nothing watches.
extern FGLObserver *gl_observer;

// [rc4l] A portal walk reported for as long as it is in scope.
struct FGLObservedWalk
{
	bool on;
	FGLObservedWalk(int kind, const void *portal, int line) : on(gl_observer != NULL)
	{
		if (!on) return;
		if (kind == 0) gl_observer->SkyboxWalkBegins();
		else if (kind == 1) gl_observer->StackWalkBegins(portal);
		else gl_observer->MirrorWalkBegins(line);
	}
	~FGLObservedWalk() { if (on && gl_observer != NULL) gl_observer->PortalWalkEnds(); }
};

#endif
