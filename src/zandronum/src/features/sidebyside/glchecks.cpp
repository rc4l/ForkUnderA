// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The checks that compare the backend's derivations against GL's own walk and draw, fed by what GL reports to its observer (gl/scene/gl_observer.h).

#include "gl/system/gl_system.h"
#include "features/sidebyside/sidebyside.h"

#include "gl/scene/gl_observer.h"
#include "gl/scene/gl_wall.h"
#include "features/stackportals/stackportals.h"   // [rc4l] FPortal
#include "gl/data/gl_matrix.h"   // [rc4l] VSMatrix
#include "gl/renderer/gl_lightdata.h"     // [rc4l] gl_SetColor and gl_SetFog, the shading GL's answer comes from
#include "gl/renderer/gl_renderstate.h"   // [rc4l] the dynamic light GL left for a sprite, and the shading it set
#include "features/horizon/horizon.h"   // [rc4l] OwnsSky
#include "features/gamecopy/actors.h"   // [rc4l] the record a skybox is named by
#include "features/levelmesh/flatmesh.h"
#include "features/levelmesh/wallcache.h"
#include "features/levelmesh/modelmesh.h"
#include "features/levelmesh/staticmesh.h"
#include "features/levelmesh/captureshading.h"
#include "features/visibility/viswalk.h"
#include "features/sprites/pspritebuild.h"
#include "features/sprites/spritebuild.h"
#include "features/sprites/computation/spritestyle_compute.h"   // [rc4l] kHwNoAlphaTest
#include "features/models/models.h"   // [Doruk-Aksoy] FSpriteModelFrame, whose flags a model's culling reads

#include "c_cvars.h"
#include "r_state.h"   // [rc4l] lines

#include <memory>
#include <vector>

EXTERN_CVAR(Bool, fua_vis_check)
EXTERN_CVAR(Bool, fua_sprite_check)
EXTERN_CVAR(Bool, fua_psprite_check)
EXTERN_CVAR(Bool, fua_shading_check)
EXTERN_CVAR(Int, r_fogmode)

namespace {

// [rc4l] GL's sprite, its corners, light, colour and blend, noted for fua_sprite_check; only noted, since GL draws before the backend's scene clears the stream.
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
	mp.mirrorView = MirrorCaptureView();
	mp.stackSlot = StackCaptureSlot();
	// [rc4l] No light index: a sprite's dynamic light is worked out per actor on the CPU and carried in dynR/G/B below.
	mp.dynLightIndex = -1;
	mp.lightLevel = spr.lightlevel;
	mp.lightColor = spr.Colormap.LightColor.d;
	mp.fadeColor = spr.Colormap.FadeColor.d;
	// [rc4l] GLSprite::Draw's own rel, none for fullbright, and ThingColor over it: a particle's own colour, a stencil actor's fill.
	const zx::sprites::SpriteDrawLook look = zx::sprites::SpriteDrawLookAt(zx::sprites::StyleOf(spr.RenderStyle), spr.trans, spr.Colormap, spr.x, spr.y, false);
	CaptureSpriteShading(spr.lightlevel, spr.fullbright ? 0 : getExtraLight(), spr.Colormap, look, spr.ThingColor.d, mp);
	// [rc4l] A fixed-colour style draws in TM_MASK, the texture only the shape.
	mp.stencil = (spr.RenderStyle.Flags & STYLEF_ColorIsFixed) != 0;
	// [rc4l] Draw's fog layer swaps the alpha test for one that passes any alpha.
	mp.alphaTest = spr.hw_styleflags != zx::sprites::kHwNoAlphaTest && !look.fogLayer;

	// [rc4l] The dynamic light gl_SetDynSpriteLight left in the render state (r_light_sprites and DONTLIGHTSELF applied), carried apart from vColor so the shader adds it after lighting as GL does.
	if (!spr.fullbright)
	{
		float dyn[3] = { 0.f, 0.f, 0.f };
		gl_RenderState.GetDynLight(dyn[0], dyn[1], dyn[2]);
		mp.dynR = dyn[0]; mp.dynG = dyn[1]; mp.dynB = dyn[2];
	}
	// [rc4l] A sprite takes this light and no other, fullbright ones none at all: GL never runs the per-light loop on one.
	mp.cpuLight = true;

	// [rc4l] A billboard has no side, so a zero normal makes the backend skip its dynamic-light side test.
	mp.normX = mp.normY = mp.normZ = 0.f;
	mp.translation = spr.translation;
	mp.blendMode = zx::sprites::StyleBlendMode(spr.RenderStyle, spr.trans);

	// [rc4l] Sprite centre, for the back-to-front sort.
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
	else out.fogMode = (out.fogColor == 0) ? (int)r_fogmode : -(int)r_fogmode;
	gl_RenderState.RestoreShading(before);
}

// [rc4l] Each report handed to the capture the checks already read: the walk bits, sky lists and portal views fua_vis_check compares, the sprites and models fua_sprite_check does, and the weapon fua_psprite_check does.
struct FGLChecks : public FGLObserver
{
	// [rc4l] The portal walk scopes open now, innermost last, each closed as its walk ends; one of the three is set.
	struct Tag
	{
		std::unique_ptr<zx::levelmesh::SkyboxWalk> skybox;
		std::unique_ptr<zx::levelmesh::StackWalk> stack;
		std::unique_ptr<zx::levelmesh::MirrorWalk> mirror;
	};
	std::vector<Tag> tags;

	void EnterScene() { zx::levelmesh::EnterScene(); }
	void LeaveScene() { zx::levelmesh::LeaveScene(); }
	void WalkBegins() { zx::levelmesh::BeginSkyWalk(); }
	void WalkedSubsector(int subsector) { zx::levelmesh::NoteWalkedSubsector(subsector); }
	void WalkedSide(int side) { zx::levelmesh::NoteWalkedSide(side); }
	// [rc4l] The skybox is passed in, never read off wall.skybox: that shares a union with wall.sky, so on a plain sky it is a GLSkyInfo.
	void SkyQuad(const GLWall &wall, AActor *skybox)
	{
		// [rc4l] GL's own captured quads are only compared, so they keep the level's sky; a horizon line's is the horizon's.
		if (zx::horizon::OwnsSky((wall.seg != NULL && wall.seg->linedef != NULL) ? wall.seg->linedef->special : 0)) return;
		zx::levelmesh::AddViewSkyQuad(zx::levelmesh::WalkSkyList(), wall.glseg.x1, wall.glseg.y1, wall.glseg.x2, wall.glseg.y2,
			wall.zbottom, wall.ztop, zx::gamecopy::IndexOf(zx::gamecopy::RecordOf(skybox)), 0);
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

	void SkyboxWalkBegins() { tags.emplace_back(); tags.back().skybox.reset(new zx::levelmesh::SkyboxWalk); }
	void StackWalkBegins(const void *portal) { tags.emplace_back(); tags.back().stack.reset(new zx::levelmesh::StackWalk(portal)); }
	void MirrorWalkBegins(int line) { tags.emplace_back(); tags.back().mirror.reset(new zx::levelmesh::MirrorWalk(line)); }
	void PortalWalkEnds() { if (!tags.empty()) tags.pop_back(); }

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
		// [rc4l] Draw turns a subtract model's style to fuzz only after setting the subtract blend, so the blend GL draws it with is the actor's.
		FRenderStyle blend = spr->RenderStyle;
		blend.BlendOp = (BYTE)zx::sprites::ComputeModelBlendOp(blend.BlendOp, spr->actor->RenderStyle.BlendOp, STYLEOP_Fuzz);
		zx::levelmesh::MeshPiece look;
		zx::levelmesh::ModelPieceLook(spr->lightlevel, spr->fullbright ? 0 : getExtraLight(), spr->Colormap, spr->fullbright, dyn,
			spr->ThingColor.d, zx::sprites::SpriteDrawLookAt(zx::sprites::StyleOf(spr->RenderStyle), spr->trans, spr->Colormap, spr->x, spr->y, true),
			zx::sprites::StyleBlendMode(blend, spr->trans), spr->x, spr->y, spr->z, look);
		look.cullFace = zx::sprites::ActorModelCull(spr->actor->RenderStyle, false, spr->modelframe->flags);
		zx::levelmesh::ModelCaptureBegin(look, model.get(), spr->actor, zx::levelmesh::kModelFromGL);
	}
	void ModelSurface(const FModelGeometry *geometry, unsigned int base1, unsigned int base2, float inter,
		unsigned int firstIndex, unsigned int count, FMaterial *tex, int translation)
	{
		zx::levelmesh::CaptureModelSurface(geometry, base1, base2, inter, firstIndex, count, tex, translation);
	}
	void ModelEnds() { zx::levelmesh::ModelCaptureEnd(); }
	void WeaponQuad(const zx::draw2d::Quad2D &q) { zx::sprites::NoteGLPSprite(q); }
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
