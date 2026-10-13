// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The checks that compare the backend's derivations against GL's own walk and draw, fed by what GL reports to its observer (gl/scene/gl_observer.h).

#include "gl/system/gl_system.h"
#include "features/sidebyside/sidebyside.h"

#include "gl/scene/gl_observer.h"
#include "gl/scene/gl_wall.h"
#include "gl/data/gl_data.h"     // [rc4l] FPortal
#include "features/levelmesh/flatmesh.h"
#include "features/levelmesh/wallcache.h"
#include "features/levelmesh/modelmesh.h"
#include "features/visibility/viswalk.h"
#include "features/sprites/pspritebuild.h"

#include "c_cvars.h"

#include <vector>

EXTERN_CVAR(Bool, fua_vis_check)
EXTERN_CVAR(Bool, fua_sprite_check)
EXTERN_CVAR(Bool, fua_psprite_check)

namespace {

// [rc4l] Each report handed to the capture the checks already read: the walk bits, sky lists and portal views fua_vis_check compares, the sprites and models fua_sprite_check does, and the weapon fua_psprite_check does.
struct FGLChecks : public FGLObserver
{
	// [rc4l] The portal walk tags open now, innermost last, each restored as its walk ends.
	struct Tag { int kind; void *walk; };
	std::vector<Tag> tags;

	void EnterScene() { zx::levelmesh::EnterScene(); }
	void LeaveScene() { zx::levelmesh::LeaveScene(); }
	void WalkBegins() { zx::levelmesh::BeginSkyWalk(); }
	void WalkedSubsector(int subsector) { zx::levelmesh::NoteWalkedSubsector(subsector); }
	void WalkedSide(int side) { zx::levelmesh::NoteWalkedSide(side); }
	void SkyQuad(const GLWall &wall, AActor *skybox) { zx::levelmesh::RegisterSkyQuad(wall, skybox); }
	void SkyHole(const GLWall &wall) { zx::levelmesh::RegisterSkyHole(wall); }
	void StackWall(const GLWall &wall, const void *portal) { zx::levelmesh::RegisterStackWall(wall, (const FPortal *)portal); }
	void MirrorSheen(const GLWall &wall) { zx::levelmesh::RegisterMirrorSheen(wall); }

	void SkyboxWalkBegins() { const Tag t = { 0, new zx::levelmesh::SkyboxWalk }; tags.push_back(t); }
	void StackWalkBegins(const void *portal) { const Tag t = { 1, new zx::levelmesh::StackWalk(portal) }; tags.push_back(t); }
	void MirrorWalkBegins(int line) { const Tag t = { 2, new zx::levelmesh::MirrorWalk(line) }; tags.push_back(t); }
	void PortalWalkEnds()
	{
		if (tags.empty()) return;
		const Tag t = tags.back();
		tags.pop_back();
		if (t.kind == 0) delete (zx::levelmesh::SkyboxWalk *)t.walk;
		else if (t.kind == 1) delete (zx::levelmesh::StackWalk *)t.walk;
		else delete (zx::levelmesh::MirrorWalk *)t.walk;
	}

	void PlayerWalkDone() { if (fua_vis_check && zx::levelmesh::PlayerWalkActive()) zx::visibility::CheckAgainstGL(); }
	void PortalViewsDone() { if (zx::levelmesh::PlayerWalkActive()) zx::visibility::CheckPortalViews(); }

	void Sprite(const GLSprite &sprite, const float *c1, const float *c2, const float *c3, const float *c4)
	{
		zx::levelmesh::RegisterSprite(sprite, c1, c2, c3, c4);
	}
	void ModelBegins(const GLSprite *sprite, const VSMatrix &model) { zx::levelmesh::ModelCaptureBegin(sprite, model); }
	void ModelSurface(FModelVertexBuffer *vbuf, unsigned int base1, unsigned int base2, float inter,
		unsigned int firstIndex, unsigned int count, FMaterial *tex, int translation)
	{
		zx::levelmesh::CaptureModelSurface(vbuf, base1, base2, inter, firstIndex, count, tex, translation);
	}
	void ModelEnds() { zx::levelmesh::ModelCaptureEnd(); }
	void WeaponQuad(const zx::hwrender::Quad2D &q) { zx::sprites::NoteGLPSprite(q); }
};

FGLChecks g_checks;

} // namespace

namespace zx { namespace sidebyside {

void WatchGLForChecks()
{
	gl_observer = (fua_vis_check || fua_sprite_check || fua_psprite_check) ? &g_checks : NULL;
}

}} // namespace zx::sidebyside
