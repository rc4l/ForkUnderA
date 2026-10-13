// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The checks that compare the backend's derivations against GL's own walk and draw, fed by what GL reports to its observer (gl/scene/gl_observer.h).

#include "gl/system/gl_system.h"
#include "features/sidebyside/sidebyside.h"

#include "gl/scene/gl_observer.h"
#include "gl/scene/gl_wall.h"
#include "gl/data/gl_data.h"     // [rc4l] FPortal
#include "gl/data/gl_matrix.h"   // [rc4l] VSMatrix
#include "gl/renderer/gl_lightdata.h"     // [rc4l] gl_SetColor and gl_SetFog, the shading GL's answer comes from
#include "gl/renderer/gl_renderstate.h"   // [rc4l] the dynamic light GL left for a sprite, and the shading it set
#include "features/levelmesh/flatmesh.h"
#include "features/levelmesh/wallcache.h"
#include "features/levelmesh/modelmesh.h"
#include "features/levelmesh/staticmesh.h"
#include "features/levelmesh/captureshading.h"
#include "features/levelmesh/computation/flatmesh_compute.h"   // [rc4l] ComputeStyleBlendMode
#include "features/visibility/viswalk.h"
#include "features/sprites/pspritebuild.h"
#include "features/sprites/spritebuild.h"
#include "features/sprites/computation/spritestyle_compute.h"   // [rc4l] kHwNoAlphaTest

#include "c_cvars.h"
#include "r_state.h"   // [rc4l] lines

#include <vector>

EXTERN_CVAR(Bool, fua_vis_check)
EXTERN_CVAR(Bool, fua_sprite_check)
EXTERN_CVAR(Bool, fua_psprite_check)
EXTERN_CVAR(Bool, fua_shading_check)
EXTERN_CVAR(Int, gl_fogmode)

namespace {

// [rc4l] GL's sprite as RegisterSprite once took it, its corners, light, colour and blend, noted for fua_sprite_check; only noted, since GL draws before the backend's scene clears the stream.
void GLSpritePiece(const GLSprite &spr, const float *c1, const float *c2, const float *c3, const float *c4)
{
	using namespace zx::levelmesh;
	// [rc4l] Each walk the backend draws again keeps its own sprites, for its pass alone: the player's for the frame, a stacked view's or a line mirror's (the viewer and whatever stands behind the player) for that one.
	if (spr.gltexture == NULL || !DynamicCaptureActive()) return;

	// [rc4l] GLSprite emits a 4-vertex TRIANGLE STRIP; the mesh is triangle lists, so expand to
	// (0,1,2) and (2,1,3) -- which preserves the strip's winding rather than flipping the second
	// triangle.
	MeshVertex q[4];
	q[0].Set(c1[0], c1[1], c1[2], spr.ul, spr.vt);
	q[1].Set(c2[0], c2[1], c2[2], spr.ur, spr.vt);
	q[2].Set(c3[0], c3[1], c3[2], spr.ul, spr.vb);
	q[3].Set(c4[0], c4[1], c4[2], spr.ur, spr.vb);

	MeshPiece mp;
	mp.range.offset = 0;
	mp.range.count = 0;
	mp.material = spr.gltexture;
	mp.mirrorLine = MirrorCaptureLine();
	mp.stackSlot = StackCaptureSlot();
	// [rc4l] GLSprite has no light index. Its dynamic light is computed per actor on the CPU and
	// folded into the vertex colour below -- see the fold after CaptureShading, which is what makes
	// that true. It was asserted here before it was done, and sprites went dark when the shader
	// stopped adding lights for them on the strength of this comment.
	mp.dynLightIndex = -1;
	mp.lightLevel = spr.lightlevel;
	mp.lightColor = spr.Colormap.LightColor.d;
	mp.fadeColor = spr.Colormap.FadeColor.d;
	// GLSprite::Draw's own `rel`: fullbright sprites take no extra light.
	// [rc4l] Not memoised, and that was measured rather than assumed -- see docs/sprites-scope.md.
	// A cache keyed on the six inputs hits 98% of 3053 sprites on Sunder MAP16 and saves nothing:
	// what it skips is two calls and eight getters, and writing the eight answers costs the same.
	CaptureShading(spr.lightlevel, spr.fullbright ? 0 : getExtraLight(), spr.Colormap, mp);
	// [rc4l] ThingColor, which GL's shader multiplies the sprite by: a particle's own colour, a stencil actor's fill.
	mp.colorR *= spr.ThingColor.r / 255.f;
	mp.colorG *= spr.ThingColor.g / 255.f;
	mp.colorB *= spr.ThingColor.b / 255.f;
	// [rc4l] A fixed-colour style draws in TM_MASK, the texture only the shape.
	mp.stencil = (spr.RenderStyle.Flags & STYLEF_ColorIsFixed) != 0;
	mp.alphaTest = spr.hw_styleflags != zx::sprites::kHwNoAlphaTest;

	// [rc4l] The dynamic light GL worked out for this actor, folded in here.
	//
	// gl_SetDynSpriteLight runs just before this and leaves its answer in the render state, where
	// main.fp picks it up as a uniform and ADDS it after the vertex colour. CaptureShading reads
	// only gl_SetColor, so the captured colour is the sector light and nothing else -- a sprite
	// standing in a green pool came out grey while GL drew it green.
	//
	// Taking it from the render state rather than walking the light list again is the whole point:
	// that function has already applied gl_light_sprites, and DONTLIGHTSELF, which is why an item is
	// not lit by the glow it carries itself. A shader loop over every light in the level knows none
	// of that, and lit each armor bonus with its own light until it was stopped.
	if (!spr.fullbright)
	{
		float dyn[3] = { 0.f, 0.f, 0.f };
		gl_RenderState.GetDynLight(dyn[0], dyn[1], dyn[2]);
		// Carried, not folded: it has to survive the lighting equation the shader applies to vColor,
		// which is where GL adds it too. Mixed into the colour it would be attenuated by distance and
		// by how dark the sector is, and a bonus in a dark room came out a quarter dimmer than GL's.
		mp.dynR = dyn[0]; mp.dynG = dyn[1]; mp.dynB = dyn[2];
	}
	// [rc4l] A sprite takes this light and no other, fullbright ones none at all: GL never runs the per-light loop on one.
	mp.cpuLight = true;

	// [rc4l] A billboard has NO side, so it gets no normal.
	//
	// CaptureShading leaves a default of straight up, which is right for a flat and meaningless for a
	// sprite: a quad that turns to face the camera has no fixed facing for a light to be in front of
	// or behind. The backend's dynamic-light side test took the default at its word and lit only the
	// fragments below the light, cutting the rest off along a dead straight horizontal line at the
	// light's own height -- a hard seam across the middle of every rocket explosion, which carries a
	// large light at its centre. Zero means "no side", and the test is skipped.
	mp.normX = mp.normY = mp.normZ = 0.f;
	mp.translation = spr.translation;
	mp.alpha = spr.trans;
	if (spr.RenderStyle.BlendOp == STYLEOP_Shadow)
		mp.blendMode = 3;
	else if (spr.RenderStyle.BlendOp == STYLEOP_Add && spr.RenderStyle.DestAlpha == STYLEALPHA_One)
		mp.blendMode = 2;
	else if (spr.trans < 1.f - 1.f/256.f || spr.RenderStyle.BlendOp != STYLEOP_Add)
		mp.blendMode = (spr.trans < 1.f - 1.f/256.f) ? 1 : 0;
	else
		mp.blendMode = 0;

	// Sprite centre, for the back-to-front sort.
	mp.sortX = spr.x; mp.sortY = spr.y; mp.sortZ = spr.z;
	// [rc4l] Four corners, not six vertices -- the backend expands them when it writes its own.
	mp.quad = true;

	zx::sprites::NoteGLSprite(spr.actor != NULL ? (const void *)spr.actor : (const void *)spr.particle, spr.actor == NULL, mp, q, 4);
}

// [rc4l] GL's own shading of a surface, read back out of the render state gl_SetColor and gl_SetFog leave, for fua_shading_check.
void GLShading(int lightlevel, int rellight, const FColormap &cm, bool noFog, bool additive, zx::levelmesh::MeshPiece &out)
{
	const FRenderState::ShadingSnapshot before = gl_RenderState.SaveShading();
	FColormap local = cm;
	gl_SetColor(lightlevel, rellight, local, 1.0f);
	if (noFog) gl_SetFog(255, 0, NULL, false);
	else       gl_SetFog(lightlevel, rellight, &local, additive);
	gl_RenderState.GetColorRGB(out.colorR, out.colorG, out.colorB);
	const float *lp = gl_RenderState.GetLightParms();
	out.softLight    = lp[3];
	out.fogDensity   = lp[2];
	out.fogColor     = gl_RenderState.GetFogColor().d & 0xffffff;
	out.desaturation = gl_RenderState.GetDesaturation();
	if (!gl_RenderState.IsFogEnabled()) out.fogMode = 0;
	else out.fogMode = (out.fogColor == 0) ? (int)gl_fogmode : -(int)gl_fogmode;
	gl_RenderState.RestoreShading(before);
}

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
	// [rc4l] The skybox is passed in, never read off wall.skybox: that shares a union with wall.sky, so on a plain sky it is a GLSkyInfo.
	void SkyQuad(const GLWall &wall, AActor *skybox)
	{
		zx::levelmesh::AddViewSkyQuad(zx::levelmesh::WalkSkyList(), wall.glseg.x1, wall.glseg.y1, wall.glseg.x2, wall.glseg.y2,
			wall.zbottom, wall.ztop, skybox, (wall.seg != NULL && wall.seg->linedef != NULL) ? wall.seg->linedef->special : 0);
	}
	void SkyHole(const GLWall &wall)
	{
		zx::levelmesh::AddViewSkyHole(zx::levelmesh::WalkSkyList(), wall.glseg.x1, wall.glseg.y1, wall.glseg.x2, wall.glseg.y2, wall.zfloor, wall.zceil);
	}
	void StackWall(const GLWall &wall, const void *portal)
	{
		if (portal == NULL || !zx::levelmesh::PlayerWalkActive()) return;
		zx::levelmesh::AddStackWall((const FPortal *)portal, wall.glseg.x1, wall.glseg.y1, wall.glseg.x2, wall.glseg.y2, wall.zbottom, wall.ztop);
	}
	void MirrorSheen(const GLWall &wall)
	{
		if (!zx::levelmesh::PlayerWalkActive() || wall.seg == NULL || wall.seg->linedef == NULL) return;
		zx::levelmesh::AddMirrorSheen(int(wall.seg->linedef - lines), wall.lightlevel, wall.Colormap);
	}

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
		GLSpritePiece(sprite, c1, c2, c3, c4);
	}
	// [rc4l] The sprite's look, with the dynamic light GL left in the render state for it.
	void ModelBegins(const GLSprite *spr, const VSMatrix &model)
	{
		float dyn[3] = { 0.f, 0.f, 0.f };
		if (!spr->fullbright) gl_RenderState.GetDynLight(dyn[0], dyn[1], dyn[2]);
		zx::levelmesh::MeshPiece look;
		zx::levelmesh::ModelPieceLook(spr->lightlevel, spr->fullbright ? 0 : getExtraLight(), spr->Colormap, spr->fullbright, dyn,
			spr->ThingColor.d, spr->trans, zx::levelmesh::ComputeStyleBlendMode(spr->RenderStyle.BlendOp == STYLEOP_Shadow,
				spr->RenderStyle.BlendOp == STYLEOP_Add && spr->RenderStyle.DestAlpha == STYLEALPHA_One, spr->trans),
			spr->x, spr->y, spr->z, look);
		zx::levelmesh::ModelCaptureBegin(look, model.get(), spr->actor, zx::levelmesh::kModelFromGL);
	}
	void ModelSurface(const FModelGeometry *geometry, unsigned int base1, unsigned int base2, float inter,
		unsigned int firstIndex, unsigned int count, FMaterial *tex, int translation)
	{
		zx::levelmesh::CaptureModelSurface(geometry, base1, base2, inter, firstIndex, count, tex, translation);
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
	zx::levelmesh::SetShadingOracle(fua_shading_check ? GLShading : NULL);
}

}} // namespace zx::sidebyside
