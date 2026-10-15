#ifndef __GL_OBSERVER_H
#define __GL_OBSERVER_H

// [rc4l] What GL reports of its own walk and draw to a check against it, called only while gl_observer is set (features/sidebyside), so GL depends on no renderer module.

#include "features/draw2d/computation/quad2d_compute.h"   // [rc4l] Quad2D, the weapon quad GL draws

class AActor;
class FMaterial;
struct FModelGeometry;
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
	virtual void ModelSurface(const FModelGeometry *geometry, unsigned int base1, unsigned int base2, float inter,
		unsigned int firstIndex, unsigned int count, FMaterial *tex, int translation) {}
	virtual void ModelEnds() {}
	virtual void WeaponQuad(const zx::draw2d::Quad2D &q) {}
};

// [rc4l] The observer GL reports to, NULL while nothing watches.
extern FGLObserver *gl_observer;

// [rc4l] The kinds of portal walk GL reports.
enum class EObservedWalk { Skybox, Stack, Mirror };

// [rc4l] A portal walk reported for as long as it is in scope: a stack's by its portal, a mirror's by its line.
struct FGLObservedWalk
{
	bool on;
	FGLObservedWalk(EObservedWalk kind, const void *portal = NULL, int line = 0) : on(gl_observer != NULL)
	{
		if (!on) return;
		if (kind == EObservedWalk::Skybox) gl_observer->SkyboxWalkBegins();
		else if (kind == EObservedWalk::Stack) gl_observer->StackWalkBegins(portal);
		else gl_observer->MirrorWalkBegins(line);
	}
	~FGLObservedWalk() { if (on && gl_observer != NULL) gl_observer->PortalWalkEnds(); }
};

// [rc4l] A scene reported for as long as it is in scope, so a check knows which scene is outermost.
struct FGLObservedScene
{
	bool on;
	FGLObservedScene() : on(gl_observer != NULL) { if (on) gl_observer->EnterScene(); }
	~FGLObservedScene() { if (on && gl_observer != NULL) gl_observer->LeaveScene(); }
};

#endif
