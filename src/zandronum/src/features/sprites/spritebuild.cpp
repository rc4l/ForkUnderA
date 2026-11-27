// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/spritebuild.h"
#include "features/gamecopy/actors.h"   // [rc4l] ActorRecord
#include "features/gamecopy/players.h"   // [rc4l] CameraOf
#include "features/gamecopy/tintsubject.h"   // [rc4l] TintSubject
#include "features/levelmesh/levelmesh.h"   // [rc4l] LightListBottom
#include "features/frame/computation/oracle_compute.h"   // [rc4l] OracleTally
#include <atomic>
#include "features/jobs/jobs.h"   // [rc4l] the pool BuildSprites derives on
#include "features/jobs/computation/joinparts_compute.h"   // [Doruk-Aksoy] Padded, each lane's chunk on lines of its own

#include "features/sprites/computation/spritestyle_compute.h"
#include "features/draw2d/computation/quad2d_compute.h"   // [rc4l] ComputeTextureMode
#include "features/hwrender/computation/fuzz_compute.h"   // [rc4l] ComputeFuzzKind
#include "features/sprites/computation/spriteframe_compute.h"   // [Doruk-Aksoy] ComputeSpriteVFlip, ComputeMirrorHidesSprite
#include "features/sprites/computation/spritequad_compute.h"
#include "features/sprites/computation/spritelight_compute.h"
#include "features/sprites/computation/spriteclip_compute.h"
#include "features/materials/spriteframe.h"   // [rc4l] gl_GetSpriteFrame
#include "features/damage-tint/damagetint.h"   // [rc4l] DamageTint_SpriteGradient
#include "features/sprites/computation/modelpose_compute.h"
#include "features/sprites/modelbuild.h"
#include <memory>   // [Doruk-Aksoy] std::unique_ptr
#include "features/sprite-roll/computation/spriteroll_compute.h"
#include "features/levelmesh/staticmesh.h"
#include "features/levelmesh/flatmesh.h"            // [rc4l] the walk the stream's pieces belong to
#include "features/levelmesh/captureshading.h"
#include "features/levelmesh/computation/shading_compute.h"   // [Doruk-Aksoy] ComputeSpriteFogLevel
#include "features/levelmesh/modelmesh.h"           // [rc4l] a derived model's capture
#include "features/levelmesh/computation/flatmesh_compute.h"
#include "features/viewsetup/spriteview.h"
#include "features/viewsetup/viewsetup.h"   // [rc4l] SpritesTakeDynamicLight
#include "features/materials/materials.h"

#include "p_local.h"
#include "d_player.h"
#include "r_defs.h"
#include "r_sky.h"
#include "r_utility.h"
#include "r_data/renderstyle.h"
#include "c_console.h"
#include "c_dispatch.h"
#include "templates.h"
#include "textures/textures.h"
#include "g_shared/a_pickups.h"
#include "p_effect.h"   // [rc4l] particle_t

// [rc4l] GL's settings and view state the derivation still reads, until the renderer interface supplies them (P5).
#include "features/rendersettings/rendersettings.h"   // [rc4l] the render settings it reads
#include "features/models/models.h"   // [rc4l] gl_FindModelFrame and the models
#include "features/modelreach/modelreach.h"   // [Doruk-Aksoy] FootprintReached
#include "features/dynlights/dynlight.h"   // [rc4l] ADynamicLight, FLightNode
#include "features/levelmesh/pointlight.h"

#include <string.h>
#include <map>
#include <set>
#include <tuple>
#include <vector>
#include "features/telemetry/telemetry.h"
#include "features/actormaterials/actormaterials.h"   // [Doruk-Aksoy] an actor's fua_Material row
#include "features/hwshaders/hwshaders.h"   // [Doruk-Aksoy] ActorMaterialShader

// [Doruk-Aksoy] Face sprites built once a tic and turned and interpolated by the vertex shader (the tic stream), off only to compare against the CPU path.
CVAR(Bool, fua_dg_gpusprites, true, CVAR_FUA_DEV_MODE)
// [Doruk-Aksoy] Picks which actors fua_dg_gpuview draws, 1 those the tic stream may carry and 2 every other piece, the same in either path; here so a headless build without the backend still links.
CVAR(Int, fua_dg_gpuview, 0, CVAR_FUA_DEV_MODE)
EXTERN_CVAR(Int, r_spriteclip)
EXTERN_CVAR(Float, r_sclipfactor)
EXTERN_CVAR(Float, r_sclipthreshold)
EXTERN_CVAR(Int, r_fuzztype)
EXTERN_CVAR(Bool, r_sprite_blend)
EXTERN_CVAR(Bool, r_usecolorblending)
EXTERN_CVAR(Bool, r_enhanced_nightvision)
EXTERN_CVAR(Int, r_enhanced_nv_stealth)
EXTERN_CVAR(Int, r_billboard_mode)
EXTERN_CVAR(Bool, r_lights)
EXTERN_CVAR(Bool, r_light_sprites)
EXTERN_CVAR(Bool, r_light_particles)
EXTERN_CVAR(Int, r_particles_style)
EXTERN_CVAR(Bool, r_billboard_particles)
EXTERN_CVAR(Float, transsouls)

// [rc4l] Derive every sprite beside GL's and compare them bit for bit, counting what differs for fua_sprite_report.
CVAR(Bool, fua_sprite_check, false, CVAR_FUA_DEV_MODE)

using zx::levelmesh::MeshPiece;
using zx::levelmesh::MeshVertex;

static_assert(STYLEOP_Add == zx::sprites::kOpAdd && STYLEOP_Sub == zx::sprites::kOpSub && STYLEOP_Fuzz == zx::sprites::kOpFuzz &&
              STYLEOP_Shadow == zx::sprites::kOpShadow, "ERenderOp must match spritestyle_compute.h");
static_assert(STYLEALPHA_Zero == zx::sprites::kAlphaZero && STYLEALPHA_One == zx::sprites::kAlphaOne &&
              STYLEALPHA_Src == zx::sprites::kAlphaSrc && STYLEALPHA_InvSrc == zx::sprites::kAlphaInvSrc, "ERenderAlpha must match");
static_assert(STYLEF_TransSoulsAlpha == zx::sprites::kFlagTransSouls && STYLEF_Alpha1 == zx::sprites::kFlagAlpha1 &&
              STYLEF_RedIsAlpha == zx::sprites::kFlagRedIsAlpha && STYLEF_FadeToBlack == zx::sprites::kFlagFadeToBlack, "ERenderFlags must match");

namespace zx { namespace sprites {

using zx::gamecopy::ActorRecord;

struct SpritePiece
{
	MeshPiece  piece;
	MeshVertex quad[4];
};

static const int kMaxBands = 16;
// [rc4l] A band and the fog layer GL draws over it.
static const int kMaxPieces = kMaxBands * 2;

int ActorModelCull(const FRenderStyle &style, bool hud, unsigned int modelFlags)
{
	const int modelCull = ((modelFlags & MDL_DONTCULLBACKFACES) ? kModelDontCull : 0) | ((modelFlags & MDL_FORCECULLBACKFACES) ? kModelForceCull : 0);
	return ComputeModelCull(style == LegacyRenderStyles[STYLE_Normal], hud, modelCull);
}

SpriteStyle StyleOf(const FRenderStyle &s)
{
	SpriteStyle o = { s.BlendOp, s.SrcAlpha, s.DestAlpha, s.Flags };
	return o;
}

FRenderStyle RenderStyleOf(const SpriteStyle &s)
{
	FRenderStyle r;
	r.AsDWORD = 0;
	r.BlendOp = (BYTE)s.blendOp;
	r.SrcAlpha = (BYTE)s.srcAlpha;
	r.DestAlpha = (BYTE)s.destAlpha;
	r.Flags = (BYTE)s.flags;
	return r;
}

int StyleBlendMode(const FRenderStyle &s, float alpha)
{
	return ComputeSpriteBlendMode(s.BlendOp, s.DestAlpha, alpha);
}

// [rc4l] The lower edge of each light list entry at the actor, as SplitSprite and SetSpriteColor measure it.
static int LightListBottoms(const ActorRecord *actor, const sector_t *sector, float *bottoms, int max)
{
	const TArray<lightlist_t> &lightlist = actor->Sector->e->rcXFloor.lightlist;
	int n = 0;
	for (unsigned i = 0; i < lightlist.Size() && n < max; i++)
		bottoms[n++] = FIXED2FLOAT(zx::levelmesh::LightListBottom(sector, i, actor->x, actor->y));
	return n;
}

// [rc4l] A light list entry's light and colour on the colormap, decoloured where the map asks.
static void TakeListLight(const lightlist_t &entry, bool clampLight, int &lightlevel, FColormap &cm)
{
	lightlevel = clampLight ? gl_ClampLight(*entry.p_lightlevel) : *entry.p_lightlevel;
	cm.CopyLightColor(entry.extra_colormap);
	if (glset.nocoloredspritelighting)
	{
		int r = cm.LightColor.r, g = cm.LightColor.g, b = cm.LightColor.b;
		ComputeDecolorize(r, g, b);
		cm.LightColor.r = r; cm.LightColor.g = g; cm.LightColor.b = b;
	}
}

// [rc4l] An actor as GLSprite::Process first takes it: whether it is drawn from here at all, where, and the model it is drawn as, if any.
struct Subject
{
	int spritenum;
	fixed_t scaleX, scaleY;
	fixed_t frac, thingx, thingy, thingz;
	fixed_t rotx, roty;   // [Doruk-Aksoy] where the rotation is picked from, this frame's position even when the corners take the tic's
	sector_t *rendersector;
	float x, y, z;
	FSpriteModelFrame *model;
};

// [Doruk-Aksoy] While true, actors are derived for the tic stream: at the tic's end, only those the shader draws as the CPU would, the rest returning nothing.
static bool g_ticDerive = false;
// [Doruk-Aksoy] Why the last tic derive left actors out of the tic stream, for fua_dg_ticstats.
enum TicWhy { kWhyNever, kWhyNoSubject, kWhyPlayerOrBob, kWhyModel, kWhyShape, kWhySector, kWhyShader, kWhyClip, kWhyGlow, kWhyShadowOrFog, kWhyLane, kWhyPieces, kWhyCount };
static std::atomic<int> g_ticWhy[kWhyCount];
static void NoteTicWhy(int why) { if (g_ticDerive) g_ticWhy[why].fetch_add(1, std::memory_order_relaxed); }
void LastTicWhy(int out[12]) { for (int i = 0; i < kWhyCount; i++) out[i] = g_ticWhy[i].load(); }
static unsigned g_ticBuilds = 0;
unsigned TakeTicBuilds() { const unsigned n = g_ticBuilds; g_ticBuilds = 0; return n; }

// [rc4l] Whether this frame's view leaves the actor out, standing at (x, y): a map section the walk missed, a camera icon outside a mirror, too close to the camera, or the view's own missile just fired.
// [Doruk-Aksoy] One test for the derive and the tic stream's per-frame check, so the two cannot disagree.
static bool ViewRejects(const ActorRecord *thing, int spritenum, fixed_t x, fixed_t y)
{
	const zx::viewsetup::SpriteViewProvider &view = zx::viewsetup::SpriteView();
	// [Doruk-Aksoy] A model wider than its radius is in view from a sector under it the walk reached, though its centre's map section is not.
	if (!(currentmapsection[thing->subsector->mapsection >> 3] & (1 << (thing->subsector->mapsection & 7))) &&
	    !zx::modelreach::FootprintReached(zx::gamecopy::IndexOf(thing))) return true;
	if (!view.PortalIsMirror() && thing->cameraIcon) return true;
	// [Doruk-Aksoy] The mirror flags, a mirror being the view GL portals through or the backend's mirror walk.
	if (ComputeMirrorHidesSprite(view.PortalIsMirror() || zx::levelmesh::MirrorCaptureView() >= 0, (thing->renderflags2 & RF2_INVISIBLEINMIRRORS) != 0,
		(thing->renderflags2 & RF2_ONLYVISIBLEINMIRRORS) != 0)) return true;
	// [rc4l] Too close to the camera to look like anything, unless it only moves up and down or is a model.
	if (P_AproxDistance(x - viewx, y - viewy) < 2 * FRACUNIT && !(thing->velx == 0 && thing->vely == 0 && thing->velz != 0) &&
	    !gl_FindModelFrame(thing->type, spritenum, thing->frame, false)) return true;
	const void *const viewActor = view.ViewActor();
	if (thing->flags & MF_MISSILE && thing->target == viewActor && viewActor != NULL &&
	    P_AproxDistance(x - viewx, y - viewy) < thing->Speed) return true;
	return view.PortalRejectsPoint(x, y);
}

// [rc4l] An actor no view draws as it stands: the view's own, without a sprite or hidden, or invisible unless night vision shows it.
// [Doruk-Aksoy] None of it changes within a tic, so the tic stream's split asks it once a tic.
static bool NeverDrawn(const ActorRecord *thing)
{
	if (thing->who == zx::viewsetup::SpriteView().ViewActor()) return true;
	if (thing->sprite == 0 || !thing->visible) return true;
	return (thing->renderflags & RF_INVISIBLE || !thing->RenderStyle.IsVisible(thing->alpha)) &&
		(!(thing->flags & MF_STEALTH) || !gl_fixedcolormap || !r_enhanced_nightvision);
}

static bool SubjectOf(const ActorRecord *thing, Subject &s)
{
	if (NeverDrawn(thing)) return false;
	s.spritenum = thing->shownSprite;
	s.scaleX = thing->shownScaleX; s.scaleY = thing->shownScaleY;
	// [Doruk-Aksoy] The tic stream carries neither a player, whose damage tint keeps per-actor state, nor a bobbing actor, whose bob is no straight line.
	if (g_ticDerive && (thing->isPlayer || (thing->flags2 & MF2_FLOATBOB))) { NoteTicWhy(kWhyPlayerOrBob); return false; }

	// [Doruk-Aksoy] The tic stream takes the tic's own position, which the shader slides back by the part of the tic not yet shown; the view's tests always take this frame's.
	s.frac = g_ticDerive ? FRACUNIT : r_ActorFrac;
	s.thingx = thing->PrevX + FixedMul(s.frac, thing->x - thing->PrevX);
	s.thingy = thing->PrevY + FixedMul(s.frac, thing->y - thing->PrevY);
	s.thingz = thing->PrevZ + FixedMul(s.frac, thing->z - thing->PrevZ);
	s.rotx = thing->PrevX + FixedMul(r_ActorFrac, thing->x - thing->PrevX);
	s.roty = thing->PrevY + FixedMul(r_ActorFrac, thing->y - thing->PrevY);
	if (ViewRejects(thing, s.spritenum, s.rotx, s.roty)) return false;

	// [rc4l] The sweep hands each actor its own sector, so GL never fakes the flat here.
	s.rendersector = thing->Sector;
	s.x = FIXED2FLOAT(s.thingx);
	s.z = FIXED2FLOAT(s.thingz - thing->floorclip);
	s.y = FIXED2FLOAT(s.thingy);
	if (thing->flags2 & MF2_FLOATBOB) s.z += FIXED2FLOAT(zx::gamecopy::BobOffset(*thing, r_TicFrac));
	s.model = gl_FindModelFrame(thing->type, s.spritenum, thing->frame, !!(thing->flags & MF_DROPPED));
	if (g_ticDerive && s.model != NULL) { NoteTicWhy(kWhyModel); return false; }
	return true;
}

// [rc4l] The light, colormap and style Process gives an actor, false when it is fully transparent; a model has no frame that could turn fullbright off or make it transparent.
struct Look
{
	bool fullbright;
	int light;   // [rc4l] its light level
	int sectorLight;   // [Doruk-Aksoy] Process's foglevel, the sector's own light, which coloured fog takes whatever the sprite's light
	FColormap cm;
	SpriteStyleOut so;
	bool inverse;      // [rc4l] drawn in TM_INVERSE, as gl_GetRenderStyle takes InvertSource
	bool colorBlend;   // [rc4l] blended by its own colour (ComputeColorBlending)
};

static bool LookOf(const ActorRecord *thing, const Subject &s, bool disableFullbright, bool textureTransparent, Look &l)
{
	// [rc4l] Fullbright unless GLDEFS turns it off for this frame, the sky-aware sector light, and a glowing floor.
	l.fullbright = (thing->flags5 & MF5_BRIGHT) || ((thing->renderflags & RF_FULLBRIGHT) && !disableFullbright);
	sector_t *rendersector = s.rendersector;
	l.light = l.fullbright ? 255 : gl_ClampLight(rendersector->rc.GetTexture(sector_t::ceiling) == skyflatnum ?
		rendersector->rc.GetCeilingLight() : rendersector->rc.GetFloorLight());
	l.sectorLight = rendersector->rc.lightlevel;
	{
		FTexture *floortex = TexMan[rendersector->rc.GetTexture(sector_t::floor)];
		if (floortex != NULL && floortex->isGlowing())
			l.light = ComputeSpriteGlow(l.light, (s.thingz - rendersector->rc.floorplane.ZatPoint(s.thingx, s.thingy)).Raw(),
				floortex->gl_info.GlowHeight);
		l.light = (BYTE)l.light;
	}

	FRenderStyle style = thing->RenderStyle;
	bool enhancedvision = false;
	if (gl_fixedcolormap)
	{
		if ((r_enhanced_nv_stealth > 0 && gl_fixedcolormap == CM_LITE) || (r_enhanced_nv_stealth == 2 && gl_fixedcolormap >= CM_TORCH) ||
		    (r_enhanced_nv_stealth == 3)) enhancedvision = true;
		l.cm.Clear();
		if (gl_fixedcolormap == CM_LITE && r_enhanced_nightvision &&
		    (thing->inventory || thing->flags3 & MF3_ISMONSTER || thing->flags & MF_MISSILE || thing->flags & MF_CORPSE))
			style.Flags |= STYLEF_InvertSource;
	}
	else
	{
		l.cm = rendersector->rc.ColorMap;
		if (l.fullbright)
		{
			int cr = l.cm.LightColor.r, cg = l.cm.LightColor.g, cb = l.cm.LightColor.b;
			ComputeFullbrightColor(false, cr, cg, cb);
			l.cm.LightColor.r = cr; l.cm.LightColor.g = cg; l.cm.LightColor.b = cb;
		}
		else if (glset.nocoloredspritelighting) l.cm.Decolorize();
	}

	if (style.BlendOp >= STYLEOP_Fuzz && style.BlendOp <= STYLEOP_FuzzOrRevSub) style.CheckFuzz();
	SpriteStyleIn si;
	si.style = StyleOf(style);
	si.alpha = FIXED2FLOAT(thing->alpha);
	si.transSouls = transsouls;
	si.fuzzType = r_fuzztype;
	si.spriteBlend = r_sprite_blend;
	si.model = s.model != NULL;
	si.textureTransparent = textureTransparent;
	si.enhancedVision = enhancedvision && r_enhanced_nightvision;
	si.stealth = !!(thing->flags & MF_STEALTH);
	si.translucent = StyleOf(LegacyRenderStyles[STYLE_Translucent]);
	l.so = ComputeSpriteStyle(si);
	l.inverse = zx::draw2d::ComputeTextureMode(l.so.style.flags, false) == zx::draw2d::kTexInverse;
	const ColorBlendIn cb = { r_usecolorblending, gl_fixedcolormap != CM_DEFAULT, s.model == NULL, l.fullbright, textureTransparent };
	l.colorBlend = ComputeColorBlending(l.so.style, l.so.trans, cb);
	return l.so.trans != 0.0f;
}

// [rc4l] Draw's touches to a piece's colormap before it is drawn: a fuzzy shadow's fade, and a style that fades to black.
static void DrawFade(const SpriteStyleOut &so, int lightlevel, FColormap &cm)
{
	if (cm.FadeColor.a == 0 && so.style.blendOp == kOpShadow && !gl_isBlack(cm.FadeColor))
		cm.FadeColor.a = ::clamp<int>(255 - lightlevel, 60, 255);
	if (so.style.flags & STYLEF_FadeToBlack) cm.FadeColor = 0;
}

SpriteDrawLook SpriteDrawLookAt(const SpriteStyle &style, float trans, const FColormap &cm, float x, float y, bool model)
{
	const float dx = x - FIXED2FLOAT(viewx), dy = y - FIXED2FLOAT(viewy);
	return ComputeSpriteDrawLook(style, trans, gl_isBlack(cm.FadeColor), cm.FadeColor.a, sqrtf(dx * dx + dy * dy), model);
}

// [rc4l] GLSprite::Draw's dynamic light for an actor: its lights, unless it is a shadow, fullbright or under a fixed colormap.
static void DrawLight(const ActorRecord *thing, const Look &l, float dyn[3])
{
	dyn[0] = dyn[1] = dyn[2] = 0.f;
	if (l.so.style.blendOp != kOpShadow && !l.fullbright && zx::viewsetup::SpritesTakeDynamicLight()) zx::levelmesh::ActorLight(*thing, dyn);
}

// [rc4l] GL's ThingColor: a stencil actor's fill, else white.
static PalEntry ThingColorOf(const ActorRecord *thing)
{
	return (thing->RenderStyle.Flags & STYLEF_ColorIsFixed) ? PalEntry(thing->fillcolor) : PalEntry(0xffffff);
}

// [Doruk-Aksoy] The drawn actor's fua_Material, which its sprite or model draws with over its texture's shader, and its uniform row.
static void ActorMaterialOf(const ActorRecord *thing, MeshPiece &mp)
{
	const int material = thing->fua_material - 1;
	mp.actorShader = zx::hwshaders::ActorMaterialShader(material);
	mp.actorRow = mp.actorShader < 0 ? 0 : zx::actormaterials::ComputeActorUniformRow(material, thing->fua_materialSlot - 1, zx::actormaterials::MaterialCount()) + 1;
}

// [rc4l] A model as Process, Draw and gl_RenderModel take it: one band, lit from the light list entry at the actor's middle, its surfaces captured under the pose's matrix.
// [Doruk-Aksoy] This half is its look and matrix, which a lane may work out.
static void ModelLookOf(const ActorRecord *thing, const Subject &s, const Look &l, MeshPiece &look, float modelToWorld[16])
{
	ActorMaterialOf(thing, look);
	int lightlevel = l.light;
	FColormap cm = l.cm;
	if (!gl_fixedcolormap && !l.fullbright)
	{
		float bottoms[kMaxBands];
		const int n = LightListBottoms(thing, thing->Sector, bottoms, kMaxBands);
		// [rc4l] SetSpriteColor, against a model's single height and the actor's y as GL measures it.
		const int entry = ComputeSpriteColorEntry(s.z, float(thing->y + (thing->height >> 1)), bottoms, n);
		if (entry >= 0) TakeListLight(thing->Sector->e->rcXFloor.lightlist[entry], false, lightlevel, cm);
	}
	// [Doruk-Aksoy] Draw picks its fog level before a fade to black clears the fog's colour.
	const int foglevel = zx::levelmesh::ComputeSpriteFogLevel(l.sectorLight, lightlevel, cm.FadeColor.d);
	DrawFade(l.so, lightlevel, cm);
	float dyn[3];
	DrawLight(thing, l, dyn);
	zx::levelmesh::ModelPieceLook(lightlevel, foglevel, l.fullbright ? 0 : getExtraLight(), cm, l.fullbright, dyn, ThingColorOf(thing).d,
		SpriteDrawLookAt(l.so.style, l.so.trans, cm, s.x, s.y, true),
		ComputeSpriteBlendMode(l.so.style.blendOp, l.so.style.destAlpha, l.so.trans), s.x, s.y, s.z, look);
	look.inverse = l.inverse;
	look.cullFace = ActorModelCull(thing->RenderStyle, false, s.model->flags);
	look.flatSurface = (s.model->flags & MDL_FUA_FLATSURFACE) != 0;

	ModelPose pose;
	ModelPoseFor(*thing, s.model, s.x, s.y, s.z, pose);
	ComputeModelMatrix(pose, modelToWorld);
}

// [Doruk-Aksoy] The translation a model's surfaces take.
static int ModelTranslationOf(const ActorRecord *thing, const FSpriteModelFrame *model)
{
	return (model->flags & MDL_IGNORETRANSLATION) ? 0 : int(thing->Translation);
}

static void DeriveModel(const ActorRecord *thing, const Subject &s, const Look &l, bool checked)
{
	MeshPiece look;
	float modelToWorld[16];
	ModelLookOf(thing, s, l, look, modelToWorld);
	zx::levelmesh::ModelCaptureBegin(look, modelToWorld, thing->who, checked ? zx::levelmesh::kModelChecked : zx::levelmesh::kModelDraw);
	CaptureModelFrames(s.model, thing->state, thing->tics, thing->type, ModelTranslationOf(thing, s.model));
	zx::levelmesh::ModelCaptureEnd();
}

// [rc4l] Derive an actor's sprite as GLSprite::Process and Draw do: the pieces drawn, 0 for none; on a worker thread (`lane`), -1 when its material is not made yet, which only the main thread may do.
static int DeriveSprite(const ActorRecord *thing, const Subject &s, SpritePiece *out, int max, bool lane = false)
{
	const zx::viewsetup::SpriteViewProvider &view = zx::viewsetup::SpriteView();
	const int spritenum = s.spritenum;
	const fixed_t scaleX = s.scaleX, scaleY = s.scaleY, thingx = s.thingx, thingy = s.thingy, frac = s.frac;
	const float x = s.x, y = s.y, z = s.z;

	// [rc4l] The rotation the viewer sees, as GL picks it.
	bool mirror = false;
	const FTextureID patch = gl_GetActorSpriteFrame(spritenum, thing->frame, R_PointToAngle(s.rotx, s.roty) - thing->angle, thing->renderflags, &mirror);
	if (!patch.isValid()) return 0;
	const int type = thing->renderflags & RF_SPRITETYPEMASK;
	// [rc4l] A floor sprite leaves GL's quad unset, so it is not drawn here either.
	if (type != RF_FACESPRITE && type != RF_WALLSPRITE) return 0;
	// [Doruk-Aksoy] Whether the tic stream may carry it, a face sprite of one band whose picture only the shader's turn and slide change within the tic; the tic derive draws nothing else.
	const extsector_t::xfloorcopy &xfloors = thing->Sector->e->rcXFloor;
	bool eligible = (g_ticDerive || fua_dg_gpuview != 0) && type == RF_FACESPRITE && !(thing->renderflags & RF_ROLLSPRITE) && !thing->isPlayer &&
		!(thing->flags2 & MF2_FLOATBOB) && xfloors.ffloors.Size() == 0 && xfloors.lightlist.Size() == 0 && thing->Sector->heightsec == NULL &&
		zx::hwshaders::ActorMaterialShader(thing->fua_material - 1) < 0 && zx::levelmesh::MirrorCaptureView() < 0 && zx::levelmesh::StackCaptureSlot() < 0;
	if (g_ticDerive && !eligible)
	{
		NoteTicWhy(type != RF_FACESPRITE || (thing->renderflags & RF_ROLLSPRITE) || thing->isPlayer || (thing->flags2 & MF2_FLOATBOB) ? kWhyShape :
			zx::hwshaders::ActorMaterialShader(thing->fua_material - 1) >= 0 ? kWhyShader : kWhySector);
		return 0;
	}
	zx::materials::MaterialRef material = NULL;
	if (!lane) material = zx::materials::MaterialForTexture(TexMan[patch], type == RF_FACESPRITE);
	else if (!zx::materials::MaterialIfMade(TexMan[patch], type == RF_FACESPRITE, material)) return -1;
	zx::materials::SpriteFrameInfo fi;
	if (!zx::materials::MaterialSpriteFrame(material, fi)) return 0;
	ComputeSpriteVFlip((thing->renderflags & RF_YFLIP) != 0, fi.vt, fi.vb);

	float ul, ur;
	const SpriteRect frame = { fi.left, fi.top, fi.width, fi.height };
	const SpriteRect r = ComputeSpriteRect(frame, fi.ul, fi.ur, mirror, FIXED2FLOAT(scaleX), FIXED2FLOAT(scaleY), ul, ur);
	float dirX, dirY;
	if (type == RF_FACESPRITE) view.ViewVector(dirX, dirY);
	else
	{
		dirX = FIXED2FLOAT(finecosine[thing->angle >> ANGLETOFINESHIFT]);
		dirY = FIXED2FLOAT(finesine[thing->angle >> ANGLETOFINESHIFT]);
	}
	SpriteEdges e = ComputeSpriteEdges(x, y, z, r, type == RF_WALLSPRITE, dirX, dirY);

	// [Doruk-Aksoy] A steady actor's clip lift and floor glow hold all through the tic, which the tic derive bakes.
	const auto &floorPlane = thing->Sector->rc.floorplane;
	const auto &ceilingPlane = thing->Sector->rc.ceilingplane;
	const bool steady = eligible && ComputeSteadyAcrossTic(
		{ thing->PrevZ.Raw(), floorPlane.ZatPoint(thing->PrevX, thing->PrevY).Raw(), ceilingPlane.ZatPoint(thing->PrevX, thing->PrevY).Raw() },
		{ thing->z.Raw(), floorPlane.ZatPoint(thing->x, thing->y).Raw(), ceilingPlane.ZatPoint(thing->x, thing->y).Raw() });
	// [rc4l] GL scales the already scaled height once more here.
	const float spriteheight = FIXED2FLOAT(scaleY) * r.height;
	const bool living = (thing->isPlayer || thing->flags3 & MF3_ISMONSTER || thing->inventory) &&
		(thing->flags & MF_ICECORPSE || !(thing->flags & MF_CORPSE));
	if (spriteheight > 0 && r_spriteclip > 0 && !(thing->flags2 & MF2_FLOATBOB) && type == RF_FACESPRITE &&
	    ComputeSpriteClips(living, r_spriteclip))
	{
		// [rc4l] The floor and ceiling GL clips against: a 3D floor the actor stands on, a height transfer, or the sector's own.
		float btm = 1000000.0f, top = -1000000.0f;
		extsector_t::xfloorcopy &xf = thing->Sector->e->rcXFloor;
		if (xf.ffloors.Size())
		{
			for (unsigned i = 0; i < xf.ffloors.Size(); i++)
			{
				F3DFloor *ff = xf.ffloors[i];
				const fixed_t floorh = ff->top.plane->ZatPoint(thingx, thingy);
				const fixed_t ceilingh = ff->bottom.plane->ZatPoint(thingx, thingy);
				if (floorh == thing->floorz) btm = FIXED2FLOAT(floorh);
				if (ceilingh == thing->ceilingz) top = FIXED2FLOAT(ceilingh);
				if (btm != 1000000.0f && top != -1000000.0f) break;
			}
		}
		else if (thing->Sector->heightsec && !(thing->Sector->heightsec->rc.MoreFlags & SECF_IGNOREHEIGHTSEC))
		{
			if (thing->flags2 & MF2_ONMOBJ && thing->floorz == thing->Sector->heightsec->rc.floorplane.ZatPoint(thingx, thingy))
			{
				btm = FIXED2FLOAT(thing->floorz);
				top = FIXED2FLOAT(thing->ceilingz);
			}
		}
		if (btm == 1000000.0f) btm = FIXED2FLOAT(thing->Sector->rc.floorplane.ZatPoint(thingx, thingy) - thing->floorclip);
		if (top == -1000000.0f) top = FIXED2FLOAT(thing->Sector->rc.ceilingplane.ZatPoint(thingx, thingy));
		SpriteClipIn ci;
		ci.living = living;
		ci.monster = !!(thing->flags3 & MF3_ISMONSTER);
		ci.noGravity = !!(thing->flags & MF_NOGRAVITY);
		ci.corpse = !!(thing->flags & MF_CORPSE);
		ci.spriteClip = r_spriteclip;
		ci.clipFactor = (float)r_sclipfactor;
		ci.clipThreshold = (float)r_sclipthreshold;
		ci.bottom = btm;
		ci.top = top;
		ci.spriteHeight = spriteheight;
		ComputeSpriteClip(ci, e.z1, e.z2);
		// [Doruk-Aksoy] The clip idle at both ends of the tic, each end at its own floor and ceiling, is idle all through it.
		for (int end = 0; end < 2 && eligible && !steady; end++)
		{
			const fixed_t ex = end ? thing->x : thing->PrevX, ey = end ? thing->y : thing->PrevY, ez = end ? thing->z : thing->PrevZ;
			const SpriteEdges ee = ComputeSpriteEdges(FIXED2FLOAT(ex), FIXED2FLOAT(ey), FIXED2FLOAT(ez - thing->floorclip), r, false, dirX, dirY);
			SpriteClipIn ce = ci;
			ce.bottom = FIXED2FLOAT(thing->Sector->rc.floorplane.ZatPoint(ex, ey) - thing->floorclip);
			ce.top = FIXED2FLOAT(thing->Sector->rc.ceilingplane.ZatPoint(ex, ey));
			eligible = ComputeSpriteClipIdle(ce, ee.z1, ee.z2);
			if (!eligible) NoteTicWhy(kWhyClip);
		}
	}

	Look look;
	if (!LookOf(thing, s, fi.disableFullbright, fi.transparent, look)) return 0;
	const bool fullbright = look.fullbright;
	const int lightlevel = look.light;
	const FColormap cm = look.cm;
	const SpriteStyleOut so = look.so;
	// [Doruk-Aksoy] Nor a glowing floor's light by height, a shadow's alpha by distance, or a fog layer (judged before Draw's fade, which only ever clears one).
	if (eligible)
	{
		FTexture *floorTex = TexMan[thing->Sector->rc.GetTexture(sector_t::floor)];
		const SpriteDrawLook drawn0 = SpriteDrawLookAt(so.style, so.trans, cm, x, y, false);
		const bool glow = floorTex != NULL && floorTex->isGlowing() && !steady;
		eligible = !glow && !drawn0.shadow && !drawn0.fogLayer;
		if (!eligible) NoteTicWhy(glow ? kWhyGlow : kWhyShadowOrFog);
	}
	if (g_ticDerive && !eligible) return 0;

	// [rc4l] Bands: one, unless a 3D floor's light list cuts the sprite; an XY billboard takes the band holding its centre instead.
	struct Band { float z1, z2, vt, vb; int light; FColormap cm; };
	Band bands[kMaxBands];
	int nbands = 0;
	const TArray<lightlist_t> &lightlist = thing->Sector->e->rcXFloor.lightlist;
	const bool xyProcess = !(thing->renderflags & RF_FORCEYBILLBOARD) && type == RF_FACESPRITE && zx::gamecopy::CameraOf(consoleplayer) != NULL &&
		(r_billboard_mode == 1 || thing->renderflags & RF_FORCEXYBILLBOARD);
	if (xyProcess || lightlist.Size() == 0 || gl_fixedcolormap || fullbright)
	{
		Band &b = bands[nbands++];
		b.z1 = e.z1; b.z2 = e.z2; b.vt = fi.vt; b.vb = fi.vb; b.light = lightlevel; b.cm = cm;
		if (xyProcess && !gl_fixedcolormap && !fullbright)
		{
			float bottoms[kMaxBands];
			const int n = LightListBottoms(thing, thing->Sector, bottoms, kMaxBands);
			// [rc4l] GL measures from the actor's y, not its z, and in raw 16.16 units; kept so the two agree.
			const int entry = ComputeSpriteColorEntry(e.z2, float(thing->y + (thing->height >> 1)), bottoms, n);
			if (entry >= 0) TakeListLight(lightlist[entry], false, b.light, b.cm);
		}
	}
	else
	{
		float bottoms[kMaxBands];
		const int n = LightListBottoms(thing, thing->Sector, bottoms, kMaxBands);
		SpriteBand split[kMaxBands];
		const int count = ComputeSpriteBands(e.z1, e.z2, fi.vt, fi.vb, bottoms, n, split);
		for (int i = 0; i < count && nbands < kMaxBands; i++)
		{
			Band &b = bands[nbands++];
			b.z1 = split[i].z1; b.z2 = split[i].z2; b.vt = split[i].vt; b.vb = split[i].vb;
			b.cm = cm;
			TakeListLight(lightlist[split[i].entry], true, b.light, b.cm);
		}
	}

	// [rc4l] Draw: the actor's dynamic light, and the corners an XY billboard or a roll turns.
	float dyn[3];
	DrawLight(thing, look, dyn);
	const bool xyDraw = !(thing->renderflags & RF_FORCEYBILLBOARD) && (r_billboard_mode == 1 || thing->renderflags & RF_FORCEXYBILLBOARD);
	const bool roll = (thing->renderflags & RF_ROLLSPRITE) != 0;
	float viewYaw = 0.f, viewPitch = 0.f;
	if (xyDraw || roll) view.ViewAngles(viewYaw, viewPitch);
	const float rollDegrees = roll ? zx::ComputeSpriteRollDegrees(thing->PrevRoll, thing->roll, frac.Raw()) : 0.f;
	const int rel = fullbright ? 0 : getExtraLight();
	const zx::gamecopy::TintSubject tint = { thing->id, thing->playerBody, thing->tintActive, thing->tintTexture };

	int drawn = 0;
	for (int i = 0; i < nbands && drawn < max; i++)
	{
		Band &b = bands[i];
		// [Doruk-Aksoy] Each band is a split copy keeping Process's foglevel, which Draw takes back for black fog before a fade to black clears the colour.
		const int foglevel = zx::levelmesh::ComputeSpriteFogLevel(look.sectorLight, b.light, b.cm.FadeColor.d);
		DrawFade(so, b.light, b.cm);

		SpriteEdges be = e;
		be.z1 = b.z1; be.z2 = b.z2;
		float c[4][3];
		// [Doruk-Aksoy] A tic-stream sprite is turned, slid and XY-billboarded by the shader, so its corners stay unrotated here.
		const bool gpuMove = g_ticDerive;
		ComputeSpriteCorners(be, xyDraw && !gpuMove, roll, viewYaw, viewPitch, rollDegrees, c);

		SpritePiece &p = out[drawn++];
		// [rc4l] A fresh record per piece, and only for the pieces drawn.
		p.piece = MeshPiece();
		p.quad[0].Set(c[0][0], c[0][1], c[0][2], ul, b.vt);
		p.quad[1].Set(c[1][0], c[1][1], c[1][2], ur, b.vt);
		p.quad[2].Set(c[2][0], c[2][1], c[2][2], ul, b.vb);
		p.quad[3].Set(c[3][0], c[3][1], c[3][2], ur, b.vb);

		MeshPiece &mp = p.piece;
		mp.range.offset = 0;
		mp.range.count = 0;
		mp.material = material;
		ActorMaterialOf(thing, mp);
		mp.mirrorView = zx::levelmesh::MirrorCaptureView();
		mp.stackSlot = zx::levelmesh::StackCaptureSlot();
		mp.dynLightIndex = -1;
		mp.lightLevel = b.light;
		mp.lightColor = b.cm.LightColor.d;
		mp.fadeColor = b.cm.FadeColor.d;
		const SpriteDrawLook drawLook = SpriteDrawLookAt(so.style, so.trans, b.cm, x, y, false);
		zx::levelmesh::CaptureSpriteShading(b.light, foglevel, rel, b.cm, drawLook, ThingColorOf(thing).d, mp);
		mp.stencil = (so.style.flags & STYLEF_ColorIsFixed) != 0;
		// [rc4l] Under a fog layer GL lets every texel with any alpha through, both passes.
		mp.alphaTest = so.hw != kHwNoAlphaTest && !drawLook.fogLayer;
		mp.inverse = look.inverse;
		mp.colorBlend = look.colorBlend;
		mp.fuzz = zx::hwrender::ComputeFuzzKind(r_fuzztype, so.overrideShader >= 0);
		// [rc4l] GLSprite::Draw's damage tint, over this band's own texture span as GL takes each band's.
		DamageTint_SpriteGradient(tint, so.style.blendOp, so.style.flags, b.vt, b.vb, mp.tint);
		if (!fullbright) { mp.dynR = dyn[0]; mp.dynG = dyn[1]; mp.dynB = dyn[2]; }
		mp.cpuLight = true;
		mp.normX = mp.normY = mp.normZ = 0.f;
		mp.translation = thing->Translation;
		mp.blendMode = ComputeSpriteBlendMode(so.style.blendOp, so.style.destAlpha, so.trans);
		mp.sortX = x; mp.sortY = y; mp.sortZ = z;
		mp.quad = true;
		mp.gpuEligible = eligible;
		mp.sortWho = zx::gamecopy::IndexOf(thing);
		if (gpuMove)
		{
			mp.gpuMove = true;
			mp.gpuXY = xyDraw;
			mp.gpuCx = x; mp.gpuCy = y;
			mp.gpuWho = zx::gamecopy::IndexOf(thing);
			mp.gpuPatch = patch.GetIndex() * 2 + (mirror ? 1 : 0);
			mp.gpuPx = -dirY; mp.gpuPy = dirX;
			mp.gpuDx = FIXED2FLOAT(thing->x - thing->PrevX);
			mp.gpuDy = FIXED2FLOAT(thing->y - thing->PrevY);
			mp.gpuDh = FIXED2FLOAT(thing->z - thing->PrevZ);
			if (thing->flags2 & MF2_FLOATBOB) mp.gpuDh += FIXED2FLOAT(zx::gamecopy::BobOffset(*thing, FRACUNIT) - zx::gamecopy::BobOffset(*thing, 0));
		}
		// [rc4l] Then the fog GL lays over it, the next piece so it sorts straight after.
		if (drawLook.fogLayer && drawn < max)
		{
			SpritePiece &f = out[drawn++];
			f = p;
			zx::levelmesh::CaptureFogLayer(b.light, foglevel, rel, b.cm, f.piece);
		}
	}
	return drawn;
}

// [Doruk-Aksoy] What every particle of a frame draws with, made on the main thread since a texture or material is made on first use.
struct ParticleLook
{
	const void *material;
	float ul, vt, ur, vb;
};

// [Doruk-Aksoy] The particle style's look, false when the style has no texture.
static bool ParticleLookOf(ParticleLook &look)
{
	// [rc4l] Only a round or smooth particle has a texture, and the backend draws no sprite without one.
	FTexture *lump = r_particles_style ? zx::materials::ParticleTexture(r_particles_style) : NULL;
	look.material = lump != NULL ? zx::materials::MaterialForTexture(lump, true) : NULL;
	if (look.material == NULL) return false;
	look.ul = 0.f; look.vt = 0.f; look.ur = 1.f; look.vb = 1.f;
	zx::materials::MaterialCorners(look.material, look.ul, look.vt, look.ur, look.vb);
	return true;
}

// [rc4l] Derive a particle as GLSprite::ProcessParticle and Draw do: false when it draws nothing, as an untextured square does not reach the backend.
static bool DeriveParticle(particle_t *particle, sector_t *sector, const ParticleLook &look, SpritePiece &p)
{
	const zx::viewsetup::SpriteViewProvider &view = zx::viewsetup::SpriteView();
	if (view.PortalRejectsPoint(particle->x, particle->y)) return false;
	if (particle->trans == 0) return false;
	const void *material = look.material;

	int lightlevel = gl_ClampLight(sector->rc.GetTexture(sector_t::ceiling) == skyflatnum ? sector->rc.GetCeilingLight() : sector->rc.GetFloorLight());
	FColormap cm;
	if (gl_fixedcolormap) cm.Clear();
	else if (!particle->bright)
	{
		const TArray<lightlist_t> &lightlist = sector->e->rcXFloor.lightlist;
		cm = sector->rc.ColorMap;
		for (unsigned i = 0; i < lightlist.Size(); i++)
		{
			const int lightbottom = i < lightlist.Size() - 1 ? (int)(lightlist[i + 1].plane.ZatPoint(particle->x, particle->y))
				: (int)(sector->rc.floorplane.ZatPoint(particle->x, particle->y));
			// [rc4l] Against the particle's y, not its z, as GL compares it.
			if (lightbottom < particle->y)
			{
				lightlevel = *lightlist[i].p_lightlevel;
				cm.LightColor = (lightlist[i].extra_colormap)->Color;
				break;
			}
		}
	}
	else
	{
		lightlevel = 255;
		cm = sector->rc.ColorMap;
		cm.ClearColor();
	}
	const float trans = particle->trans / 255.0f;
	PalEntry color = particle->color;
	color.a = 255;
	const float ul = look.ul, vt = look.vt, ur = look.ur, vb = look.vb;

	const float x = FIXED2FLOAT(particle->x), y = FIXED2FLOAT(particle->y), z = FIXED2FLOAT(particle->z);
	float scalefac = particle->size / 4.0f;
	// [BB] The smooth particles are smaller than the other ones.
	if (r_particles_style == 2) scalefac *= 1.7;
	float vx, vy;
	view.ViewVector(vx, vy);
	const SpriteEdges e = { x + vy * scalefac, y - vx * scalefac, x - vy * scalefac, y + vx * scalefac, z - scalefac, z + scalefac };
	float viewYaw = 0.f, viewPitch = 0.f;
	if (r_billboard_particles) view.ViewAngles(viewYaw, viewPitch);
	float c[4][3];
	ComputeSpriteCorners(e, r_billboard_particles, false, viewYaw, viewPitch, 0.f, c);

	p.piece = MeshPiece();
	p.quad[0].Set(c[0][0], c[0][1], c[0][2], ul, vt);
	p.quad[1].Set(c[1][0], c[1][1], c[1][2], ur, vt);
	p.quad[2].Set(c[2][0], c[2][1], c[2][2], ul, vb);
	p.quad[3].Set(c[3][0], c[3][1], c[3][2], ur, vb);
	MeshPiece &mp = p.piece;
	mp.range.offset = 0;
	mp.range.count = 0;
	mp.material = material;
	mp.mirrorView = zx::levelmesh::MirrorCaptureView();
	mp.stackSlot = zx::levelmesh::StackCaptureSlot();
	mp.dynLightIndex = -1;
	mp.lightLevel = lightlevel;
	mp.lightColor = cm.LightColor.d;
	mp.fadeColor = cm.FadeColor.d;
	// [Doruk-Aksoy] ProcessParticle's foglevel is the sector's light, so a bright particle is fogged as its room is.
	const int foglevel = zx::levelmesh::ComputeSpriteFogLevel(sector->rc.lightlevel, lightlevel, cm.FadeColor.d);
	zx::levelmesh::CaptureSpriteShading(lightlevel, foglevel, getExtraLight(), cm, ComputePlainDrawLook(trans), color.d, mp);
	float dyn[3] = { 0.f, 0.f, 0.f };
	if (r_lights && view.DynamicLightCount() && !gl_fixedcolormap && r_light_particles)
		zx::levelmesh::PointLight(particle->x, particle->y, particle->z, particle->subsector, NULL, true, dyn);
	mp.dynR = dyn[0]; mp.dynG = dyn[1]; mp.dynB = dyn[2];
	mp.cpuLight = true;
	mp.normX = mp.normY = mp.normZ = 0.f;
	mp.translation = 0;
	// [rc4l] STYLE_Translucent: blended unless it is fully opaque.
	mp.blendMode = trans < 1.f - 1.f / 256.f ? 1 : 0;
	mp.alphaTest = ComputeParticleAlphaTested(r_particles_style, trans);
	mp.sortX = x; mp.sortY = y; mp.sortZ = z;
	mp.quad = true;
	return true;
}

// [rc4l] The check against GL.

struct Noted
{
	const void *who;
	bool particle;
	MeshPiece piece;
	std::vector<MeshVertex> verts;
};
static std::vector<Noted> g_gl, g_ours;
static zx::frame::OracleTally g_check;   // [rc4l] actors compared, and how many differed
static int g_onlyGL = 0, g_onlyOurs = 0;
// [rc4l] Model surfaces among the pieces compared, so a clean report is known to have had models in it.
static int g_checkedModelSurfaces = 0;

static void Note(std::vector<Noted> &list, const void *who, bool particle, const MeshPiece &piece, const MeshVertex *verts, int count)
{
	Noted n;
	n.who = who;
	n.particle = particle;
	n.piece = piece;
	n.verts.assign(verts, verts + count);
	list.push_back(n);
}

void NoteGLSprite(const void *who, bool particle, const MeshPiece &piece, const MeshVertex *verts, int count)
{
	if (fua_sprite_check) Note(g_gl, who, particle, piece, verts, count);
}

void NoteDerivedSprite(const void *who, const MeshPiece &piece, const MeshVertex *verts, int count)
{
	if (fua_sprite_check) Note(g_ours, who, false, piece, verts, count);
}

static bool Same(float a, float b) { return memcmp(&a, &b, sizeof(float)) == 0; }

// [rc4l] The fields the backend reads from a sprite piece or model surface, compared bit for bit; the first that differs, or NULL.
static const char *Differs(const Noted &a, const Noted &b)
{
	const MeshPiece &x = a.piece, &y = b.piece;
	if (x.material != y.material) return "material";
	if (a.verts.size() != b.verts.size()) return "vertex count";
	for (size_t i = 0; i < a.verts.size(); i++)
		if (!Same(a.verts[i].x, b.verts[i].x) || !Same(a.verts[i].y, b.verts[i].y) || !Same(a.verts[i].z, b.verts[i].z)) return "corner";
	for (size_t i = 0; i < a.verts.size(); i++)
		if (!Same(a.verts[i].u, b.verts[i].u) || !Same(a.verts[i].v, b.verts[i].v)) return "uv";
	if (x.lightLevel != y.lightLevel || x.lightColor != y.lightColor || x.fadeColor != y.fadeColor) return "light";
	if (!Same(x.colorR, y.colorR) || !Same(x.colorG, y.colorG) || !Same(x.colorB, y.colorB) || !Same(x.softLight, y.softLight) ||
	    !Same(x.fogDensity, y.fogDensity) || x.fogColor != y.fogColor || x.fogMode != y.fogMode || x.desaturation != y.desaturation) return "shading";
	if (!Same(x.dynR, y.dynR) || !Same(x.dynG, y.dynG) || !Same(x.dynB, y.dynB)) return "dynamic light";
	if (x.translation != y.translation) return "translation";
	if (!Same(x.alpha, y.alpha) || x.blendMode != y.blendMode) return "style";
	if (!Same(x.sortX, y.sortX) || !Same(x.sortY, y.sortY) || !Same(x.sortZ, y.sortZ)) return "sort point";
	if (x.mirrorView != y.mirrorView || x.stackSlot != y.stackSlot) return "walk";
	if (x.solid != y.solid) return "solid";
	if (x.cullFace != y.cullFace) return "cull";
	if (x.stencil != y.stencil) return "stencil";
	if (x.alphaTest != y.alphaTest) return "alpha test";
	return NULL;
}

// [rc4l] The record of the actor the check noted, for its report.
static const ActorRecord *RecordOf(const void *who)
{
	int count;
	const ActorRecord *recs = zx::gamecopy::Actors(count);
	for (int i = 0; i < count; i++) if (recs[i].who == who) return &recs[i];
	return NULL;
}

void CheckFrameSprites()
{
	if (!fua_sprite_check) { g_gl.clear(); g_ours.clear(); return; }
	// [rc4l] Both lists are in actor order within an actor and a walk; match each actor's run of pieces in one walk against the other's in the same walk, so a mirror's view of an actor is not held against the player's.
	typedef std::tuple<const void *, int, int> RunKey;
	const auto keyOf = [](const Noted &n) { return RunKey(n.who, n.piece.mirrorView, n.piece.stackSlot); };
	std::map<RunKey, unsigned> glFirst;
	for (unsigned i = 0; i < g_gl.size(); i++) glFirst.insert(std::make_pair(keyOf(g_gl[i]), i));
	std::set<RunKey> seen;
	for (unsigned i = 0; i < g_ours.size(); i++) if (!g_ours[i].piece.quad) g_checkedModelSurfaces++;
	for (unsigned i = 0; i < g_ours.size(); )
	{
		const RunKey key = keyOf(g_ours[i]);
		const void *a = g_ours[i].who;
		unsigned j = i;
		while (j < g_ours.size() && keyOf(g_ours[j]) == key) j++;
		seen.insert(key);
		const auto first = glFirst.find(key);
		// [rc4l] An actor GL's walk did not reach but the frustum sweep did is not GL's to compare, so it is only counted.
		if (first == glFirst.end()) { g_onlyOurs++; i = j; continue; }
		g_check.checked++;
		unsigned k = first->second, n = 0;
		const char *why = NULL;
		for (; k < g_gl.size() && keyOf(g_gl[k]) == key; k++, n++)
		{
			if (i + n >= j) { why = "piece count"; break; }
			why = Differs(g_gl[k], g_ours[i + n]);
			if (why) break;
		}
		if (!why && i + n != j) why = "piece count";
		if (why)
		{
			if (zx::frame::ComputeOracleDiffer(g_check, 12))
			{
				const MeshVertex none = {};
				const MeshVertex &gp = g_gl[first->second].verts.empty() ? none : g_gl[first->second].verts[0];
				const MeshVertex &op = g_ours[i].verts.empty() ? none : g_ours[i].verts[0];
				const bool part = g_ours[i].particle;
				const ActorRecord *rec = part ? NULL : RecordOf(a);
				zx::frame::ComputeOracleNote(g_check, "  %s at (%.0f, %.0f): %s; GL corner %.9g %.9g %.9g, ours %.9g %.9g %.9g\n",
					part ? "particle" : rec != NULL ? rec->type->TypeName.GetChars() : "actor",
					FIXED2FLOAT(part ? ((const particle_t *)a)->x : rec != NULL ? rec->x : 0),
					FIXED2FLOAT(part ? ((const particle_t *)a)->y : rec != NULL ? rec->y : 0), why,
					gp.x, gp.z, gp.y, op.x, op.z, op.y);
			}
		}
		i = j;
	}
	for (unsigned i = 0; i < g_gl.size(); i++)
		if (seen.insert(keyOf(g_gl[i])).second) g_onlyGL++;
	g_gl.clear();
	g_ours.clear();
}

void BuildSprite(const ActorRecord &record)
{
	const ActorRecord *thing = &record;
	Subject s;
	if (!SubjectOf(thing, s)) return;
	if (s.model != NULL)
	{
		Look l;
		if (LookOf(thing, s, false, false, l)) DeriveModel(thing, s, l, fua_sprite_check);
		return;
	}
	// [rc4l] Kept between actors: constructing sixteen records an actor cost more than deriving it.
	static SpritePiece pieces[kMaxPieces];
	const int n = DeriveSprite(thing, s, pieces, kMaxPieces);
	// [rc4l] GL reports a sprite once, before its fog layer, so the check compares that piece alone.
	if (fua_sprite_check)
		for (int i = 0; i < n; i++) if (!pieces[i].piece.fogPass) Note(g_ours, thing->who, false, pieces[i].piece, pieces[i].quad, 4);
	if (!zx::levelmesh::DynamicCaptureActive()) return;
	for (int i = 0; i < n; i++) zx::levelmesh::DynAppend(pieces[i].quad, 4, pieces[i].piece);
}

// [rc4l] An actor a lane derived: its pieces in the lane's list, or that the main thread derives it.
// [Doruk-Aksoy] `model` is its derived model among the chunk's, or -1.
struct LaneSprite
{
	int first, count;
	bool onGameThread;
	int model;
};

// [Doruk-Aksoy] A model a lane derived: its look, its matrix and its run of the chunk's resolved surfaces.
struct LaneModel
{
	MeshPiece look;
	float matrix[16];
	unsigned firstSurface, surfaces;
};

// [Doruk-Aksoy] A chunk's models this frame, with their surfaces and those surfaces' corners.
struct LaneModels
{
	std::vector<LaneModel> models;
	std::vector<ResolvedModelSurface> surfaces;
	std::vector<MeshVertex> verts;
};

// [rc4l] One chunk of a batch's actors, derived on a lane.
struct SpriteChunk
{
	std::vector<SpritePiece> pieces;   // [Doruk-Aksoy] only grows, since sizing it to each actor's kMaxPieces constructed them all again; `used` of them are this frame's
	int used = 0;
	std::vector<LaneSprite> sprites;
	std::unique_ptr<LaneModels> models;   // [Doruk-Aksoy] made with the chunk's first model, apart from the chunk so it keeps to one line
};

// [Doruk-Aksoy] DeriveModel on a lane into the chunk, for the main thread to hand on in the sweep's order; false when the main thread must derive it.
static bool DeriveLaneModel(const ActorRecord *thing, const Subject &s, SpriteChunk &c, LaneSprite &ls)
{
	Look l;
	if (!LookOf(thing, s, false, false, l)) return true;
	if (!c.models) c.models.reset(new LaneModels);
	LaneModels &store = *c.models;
	LaneModel m;
	ModelLookOf(thing, s, l, m.look, m.matrix);
	m.firstSurface = (unsigned)store.surfaces.size();
	if (!ResolveModelFrames(s.model, thing->state, thing->tics, thing->type, ModelTranslationOf(thing, s.model), m.matrix, store.surfaces, store.verts,
		!fua_sprite_check, m.look.flatSurface)) return false;
	m.surfaces = (unsigned)store.surfaces.size() - m.firstSurface;
	ls.model = (int)store.models.size();
	store.models.push_back(m);
	return true;
}

// [Doruk-Aksoy] A lane's model handed to the capture as DeriveModel hands its own.
static void EmitLaneModel(const LaneModels &c, const LaneModel &m, const void *who)
{
	zx::levelmesh::ModelCaptureBegin(m.look, m.matrix, who, fua_sprite_check ? zx::levelmesh::kModelChecked : zx::levelmesh::kModelDraw);
	for (unsigned k = 0; k < m.surfaces; k++)
	{
		const ResolvedModelSurface &f = c.surfaces[m.firstSurface + k];
		if (f.count == 0) zx::levelmesh::ModelSurfaceSkipped();
		else if (f.gpuGeometry != NULL) zx::levelmesh::EmitGpuModelSurface(f.gpuGeometry, f.base1, f.base2, f.inter, f.firstIndex, f.count, f.material, f.translation);
		else zx::levelmesh::EmitModelSurface(&c.verts[f.first], f.count, f.material, f.translation);
	}
	zx::levelmesh::ModelCaptureEnd();
}

// [Doruk-Aksoy] Padded, since lanes take neighbouring chunks at once and each actor wrote a line both chunks' headers were in.
static std::vector<zx::jobs::Padded<SpriteChunk>> g_spriteChunks;
// [rc4l] Actors a lane derives at a time: enough that a chunk outweighs taking it, few enough to spread a frame's actors.
static const int kSpriteChunk = 32;

static void DeriveChunk(const ActorRecord *const *things, int count, int chunk)
{
	SpriteChunk &c = g_spriteChunks[chunk].value;
	c.used = 0;
	c.sprites.clear();
	if (c.models)
	{
		c.models->models.clear();
		c.models->surfaces.clear();
		c.models->verts.clear();
	}
	const int end = (chunk + 1) * kSpriteChunk < count ? (chunk + 1) * kSpriteChunk : count;
	for (int i = chunk * kSpriteChunk; i < end; i++)
	{
		LaneSprite ls = { c.used, 0, false, -1 };
		Subject s;
		if (SubjectOf(things[i], s))
		{
			if (things[i]->isPlayer) ls.onGameThread = true;   // [rc4l] a player's damage tint keeps per-actor state
			else if (s.model != NULL) ls.onGameThread = !DeriveLaneModel(things[i], s, c, ls);
			else
			{
				// [Doruk-Aksoy] DeriveSprite writes every piece it draws afresh, so last frame's records are reused as they stand.
				if ((int)c.pieces.size() < c.used + kMaxPieces) c.pieces.resize(c.used + kMaxPieces);
				const int n = DeriveSprite(things[i], s, &c.pieces[c.used], kMaxPieces, true);
				ls.onGameThread = n < 0;
				ls.count = n < 0 ? 0 : n;
				c.used += ls.count;
			}
		}
		c.sprites.push_back(ls);
	}
}

// [Doruk-Aksoy] Below this many items a run is appended one at a time, since sharing it out costs more than it saves.
static const unsigned kPlaceAlone = 128;
// [Doruk-Aksoy] Items a lane places at a time.
static const int kPlaceChunk = 256;

// [Doruk-Aksoy] One thing the gather puts on the stream, a derived quad or a lane's model, and where in the run PlaceItems lands it.
struct PlaceItem
{
	const SpritePiece *quad;   // [Doruk-Aksoy] NULL for a model
	const LaneModels *store;
	int model;
	const void *who;
	unsigned piece, vert;
	int matrix;
	unsigned surfaces, verts, triangles;   // [Doruk-Aksoy] a model's, from ModelExtent
};

// [Doruk-Aksoy] A lane model's surfaces drawn, their corners and their triangles.
static void ModelExtent(const LaneModels &store, const LaneModel &m, unsigned &surfaces, unsigned &verts, unsigned &triangles)
{
	surfaces = verts = triangles = 0;
	for (unsigned k = 0; k < m.surfaces; k++)
	{
		const ResolvedModelSurface &f = store.surfaces[m.firstSurface + k];
		if (f.count == 0) continue;
		surfaces++;
		if (f.gpuGeometry == NULL) verts += f.count;   // [Doruk-Aksoy] a GPU surface's corners are not in the stream's vertices
		triangles += f.count / 3;
	}
}

// [Doruk-Aksoy] A lane model counted for fua_models and its matrix added, as its surfaces' appends would have; the matrix's index.
static int CountPlacedModel(const PlaceItem &it)
{
	const LaneModels &store = *it.store;
	const LaneModel &m = store.models[it.model];
	const void *last = NULL;
	const MeshVertex *first = NULL;
	for (unsigned k = 0; k < m.surfaces; k++)
	{
		const ResolvedModelSurface &f = store.surfaces[m.firstSurface + k];
		if (f.count == 0) continue;
		if (first == NULL && f.gpuGeometry == NULL) first = &store.verts[f.first];
		last = f.material;
	}
	return zx::levelmesh::ModelPlaced(m.matrix, m.look, last, first, it.verts, it.surfaces, it.triangles, m.surfaces - it.surfaces);
}

// [Doruk-Aksoy] One gathered item written into its place in the run.
static void PlaceOne(const zx::levelmesh::DynQuadRun &at, const zx::levelmesh::ModelPlacing &placing, const PlaceItem &it)
{
	if (it.quad != NULL) { zx::levelmesh::DynPlace(at, it.piece, it.vert, it.quad->quad, 4, it.quad->piece); return; }
	const LaneModels &store = *it.store;
	const LaneModel &m = store.models[it.model];
	unsigned piece = it.piece, vert = it.vert;
	for (unsigned k = 0; k < m.surfaces; k++)
	{
		const ResolvedModelSurface &f = store.surfaces[m.firstSurface + k];
		if (f.count == 0) continue;
		MeshPiece mp;
		zx::levelmesh::ModelSurfacePiece(m.look, placing, f.material, f.translation, it.matrix, mp);
		if (f.gpuGeometry != NULL)
		{
			zx::levelmesh::GpuModelSurfacePiece(f.gpuGeometry, f.base1, f.base2, f.inter, f.firstIndex, mp);
			zx::levelmesh::DynPlaceGpuModel(at, piece++, f.count, mp);
			continue;
		}
		zx::levelmesh::DynPlace(at, piece++, vert, &store.verts[f.first], f.count, mp);
		vert += f.count;
	}
}

// [Doruk-Aksoy] The gathered items put on the dynamic stream in order, written in place across the pool when there are enough, as appending them one at a time would.
static void PlaceItems(std::vector<PlaceItem> &items)
{
	const unsigned n = (unsigned)items.size();
	if (n == 0) return;
	// [Doruk-Aksoy] A run the pool may place wakes it first, so its workers are spinning once the run is laid out and its models counted.
	if (n >= kPlaceAlone) zx::jobs::Pool().Wake();
	unsigned verts = 0, pieces = 0;
	bool models = false;
	for (PlaceItem &it : items)
	{
		it.piece = pieces;
		it.vert = verts;
		if (it.quad != NULL) { pieces++; verts += 4; continue; }
		ModelExtent(*it.store, it.store->models[it.model], it.surfaces, it.verts, it.triangles);
		pieces += it.surfaces;
		verts += it.verts;
		models = true;
	}
	const zx::levelmesh::ModelPlacing placing = zx::levelmesh::ModelPlacingNow();
	zx::levelmesh::DynQuadRun at;
	// [Doruk-Aksoy] fua_sprite_check notes each model surface as the capture hands it over, so it keeps the one-at-a-time path.
	if (n < kPlaceAlone || (models && (fua_sprite_check || !placing.active)) || !zx::levelmesh::DynReserve(verts, pieces, at))
	{
		for (const PlaceItem &it : items)
		{
			if (it.quad != NULL) zx::levelmesh::DynAppend(it.quad->quad, 4, it.quad->piece);
			else EmitLaneModel(*it.store, it.store->models[it.model], it.who);
		}
		return;
	}
	// [Doruk-Aksoy] Each model counted and its matrix added in order, as its first surface's append did.
	for (PlaceItem &it : items)
		if (it.quad == NULL) it.matrix = CountPlacedModel(it);
	auto place = [&items, &at, &placing](int lo, int hi, int)
	{
		for (int i = lo; i < hi; i++) PlaceOne(at, placing, items[i]);
	};
	zx::jobs::Pool().ParallelChunks((int)n, kPlaceChunk, place);
}

void BuildSprites(const ActorRecord *const *things, int count)
{
	ZX_ZONE("scene/sprites");   // [rc4l] the actors in view derived as sprites
	if (count <= 0) return;
	const int chunks = (count + kSpriteChunk - 1) / kSpriteChunk;
	if ((int)g_spriteChunks.size() < chunks) g_spriteChunks.resize(chunks);
	struct Derive
	{
		const ActorRecord *const *things;
		int count;
		void operator()(int chunk, int) const { DeriveChunk(things, count, chunk); }
	} derive = { things, count };
	zx::jobs::Pool().ParallelFor(chunks, derive);
	const bool capture = zx::levelmesh::DynamicCaptureActive();
	// [Doruk-Aksoy] The lanes' quads and models gathered in order, each run put on the stream before an actor the main thread derives, so the stream keeps the sweep's order.
	static std::vector<PlaceItem> run;
	run.clear();
	for (int c = 0; c < chunks; c++)
	{
		const SpriteChunk &chunk = g_spriteChunks[c].value;
		for (size_t k = 0; k < chunk.sprites.size(); k++)
		{
			const LaneSprite &ls = chunk.sprites[k];
			if (ls.onGameThread)
			{
				PlaceItems(run);
				run.clear();
				BuildSprite(*things[c * kSpriteChunk + (int)k]);
			}
			else if (!capture) continue;
			else if (ls.model >= 0) run.push_back(PlaceItem{ NULL, chunk.models.get(), ls.model, things[c * kSpriteChunk + (int)k]->who, 0, 0, -1, 0, 0, 0 });
			else for (int i = 0; i < ls.count; i++) run.push_back(PlaceItem{ &chunk.pieces[ls.first + i], NULL, -1, NULL, 0, 0, -1, 0, 0, 0 });
		}
	}
	PlaceItems(run);
}

// [Doruk-Aksoy] One chunk of the tic derive, each actor's piece in the chunk's list, -1 for none, or -2 when only the main thread may derive it.
struct TicChunk
{
	std::vector<SpritePiece> pieces;
	std::vector<int> got;
};
static std::vector<zx::jobs::Padded<TicChunk>> g_ticChunks;

// [Doruk-Aksoy] The tic piece each record of the fill is drawn by, -1 for none, -2 for one no view draws this tic.
static std::vector<int> g_ticPieceOf;

static void DeriveTicChunk(const ActorRecord *const *things, int count, int chunk)
{
	TicChunk &c = g_ticChunks[chunk].value;
	c.got.clear();
	int used = 0;
	const int end = (chunk + 1) * kSpriteChunk < count ? (chunk + 1) * kSpriteChunk : count;
	for (int i = chunk * kSpriteChunk; i < end; i++)
	{
		Subject s;
		int got = -1;
		if (NeverDrawn(things[i])) got = -3;
		else if (SubjectOf(things[i], s))
		{
			if ((int)c.pieces.size() < used + kMaxPieces) c.pieces.resize(used + kMaxPieces);
			const int n = DeriveSprite(things[i], s, &c.pieces[used], kMaxPieces, true);
			if (n < 0) { got = -2; NoteTicWhy(kWhyLane); }
			else if (n == 1) got = used++;
			else if (n > 1) NoteTicWhy(kWhyPieces);
		}
		else NoteTicWhy(kWhyNoSubject);
		if (got == -3) NoteTicWhy(kWhyNever);
		c.got.push_back(got);
	}
}

void BuildTicSprites(const ActorRecord *const *things, int count)
{
	g_ticBuilds++;
	ZX_ZONE("scene/tic_sprites");   // [Doruk-Aksoy] the tic stream derived, on a tic's first frame
	int records = 0;
	zx::gamecopy::Actors(records);
	g_ticPieceOf.assign(records, -1);
	if (count <= 0) { const zx::levelmesh::DynTicScope scope; return; }
	const int chunks = (count + kSpriteChunk - 1) / kSpriteChunk;
	if ((int)g_ticChunks.size() < chunks) g_ticChunks.resize(chunks);
	for (std::atomic<int> &w : g_ticWhy) w.store(0);
	g_ticDerive = true;
	struct Derive
	{
		const ActorRecord *const *things;
		int count;
		void operator()(int chunk, int) const { DeriveTicChunk(things, count, chunk); }
	} derive = { things, count };
	zx::jobs::Pool().ParallelFor(chunks, derive);
	// [Doruk-Aksoy] Those whose material a lane could not make, derived here, their pieces kept apart until every one is in.
	static std::vector<SpritePiece> mainPieces;
	static std::vector<int> mainAt;
	mainAt.assign(count, -1);
	int mainUsed = 0;
	for (int i = 0; i < count; i++)
	{
		if (g_ticChunks[i / kSpriteChunk].value.got[i % kSpriteChunk] != -2) continue;
		Subject s;
		if (!SubjectOf(things[i], s)) continue;
		if ((int)mainPieces.size() < mainUsed + kMaxPieces) mainPieces.resize(mainUsed + kMaxPieces);
		if (DeriveSprite(things[i], s, &mainPieces[mainUsed], kMaxPieces) == 1) mainAt[i] = mainUsed++;
	}
	g_ticDerive = false;
	ZX_ZONE("scene/tic_append");   // [Doruk-Aksoy] the derived pieces put on the tic stream
	// [Doruk-Aksoy] Gathered in sweep order with their records, so one run is reserved for all and each placed on its own lane.
	static std::vector<const SpritePiece *> quads;
	static std::vector<int> owners;
	quads.clear();
	owners.clear();
	for (int i = 0; i < count; i++)
	{
		const int got = g_ticChunks[i / kSpriteChunk].value.got[i % kSpriteChunk];
		const int at = zx::gamecopy::IndexOf(things[i]);
		if (got == -3)
		{
			if (at >= 0 && at < records) g_ticPieceOf[at] = -2;
			continue;
		}
		const SpritePiece *p = got >= 0 ? &g_ticChunks[i / kSpriteChunk].value.pieces[got] : mainAt[i] >= 0 ? &mainPieces[mainAt[i]] : NULL;
		if (p == NULL) continue;
		quads.push_back(p);
		owners.push_back(at);
	}
	const zx::levelmesh::DynTicScope scope;
	const int n = (int)quads.size();
	int placed = 0;
	static std::vector<int> pieceAt;   // [Doruk-Aksoy] each gathered quad's index in the tic stream, -1 for one that did not fit
	pieceAt.assign(n, -1);
	zx::levelmesh::DynQuadRun run;
	if (n > 0 && zx::levelmesh::DynReserve(4u * (unsigned)n, (unsigned)n, run))
	{
		auto put = [&](int lo, int hi, int) { for (int k = lo; k < hi; k++) zx::levelmesh::DynPlace(run, (unsigned)k, 4u * (unsigned)k, quads[k]->quad, 4, quads[k]->piece); };
		zx::jobs::Pool().ParallelChunks(n, 256, put);
		for (int k = 0; k < n; k++) pieceAt[k] = k;
		placed = n;
	}
	// [Doruk-Aksoy] Past the stream's ceiling, as many as fit one at a time; the rest are derived each frame instead.
	else for (int k = 0; k < n; k++)
	{
		zx::levelmesh::DynAppend(quads[k]->quad, 4, quads[k]->piece);
		int now = 0;
		zx::levelmesh::DynTicPieces(now);
		if (now == placed) continue;
		pieceAt[k] = placed;
		placed = now;
	}
	for (int k = 0; k < n; k++)
		if (pieceAt[k] >= 0 && owners[k] >= 0 && owners[k] < records) g_ticPieceOf[owners[k]] = pieceAt[k];
	ZX_COUNT("ticstream/tic_pieces", "sprites", placed);
}

// [Doruk-Aksoy] Whether a tic piece still draws its actor as the CPU would this frame: the view keeps it at this frame's position, and the rotation picked from there is the piece's.
static bool TicPieceHolds(const ActorRecord *thing, const MeshPiece &p)
{
	const fixed_t rx = thing->PrevX + FixedMul(r_ActorFrac, thing->x - thing->PrevX);
	const fixed_t ry = thing->PrevY + FixedMul(r_ActorFrac, thing->y - thing->PrevY);
	if (ViewRejects(thing, thing->shownSprite, rx, ry)) return false;
	bool mirror = false;
	const FTextureID patch = gl_GetActorSpriteFrame(thing->shownSprite, thing->frame, R_PointToAngle(rx, ry) - thing->angle, thing->renderflags, &mirror);
	return patch.GetIndex() * 2 + (mirror ? 1 : 0) == p.gpuPatch;
}

// [Doruk-Aksoy] The last split's tic pieces shown and actors derived for the frame, for fua_dg_ticstats.
static int g_lastShown = 0, g_lastFrameDerived = 0;
void LastTicSplit(int &shown, int &frameDerived) { shown = g_lastShown; frameDerived = g_lastFrameDerived; }

void SplitFrameSprites(const ActorRecord *const *things, int count)
{
	ZX_ZONE("scene/tic_split");   // [Doruk-Aksoy] this frame's actors shared between the tic stream and the frame's
	int hiddenCount = 0, ticCount = 0;
	unsigned char *hidden = zx::levelmesh::DynTicHidden(hiddenCount);
	const MeshPiece *tic = zx::levelmesh::DynTicPieces(ticCount);
	for (int i = 0; i < hiddenCount; i++) hidden[i] = 1;
	static std::vector<const ActorRecord *> frame;
	// [Doruk-Aksoy] Checked across the pool: each actor holds at most one tic piece, so lanes clear different flags, and each chunk lists its frame actors to be joined in order.
	zx::jobs::JobPool &pool = zx::jobs::Pool();
	const int chunk = zx::jobs::ComputeChunkFor(count, 256, pool.Workers());
	static std::vector<zx::jobs::Part<const ActorRecord *>> parts;
	zx::jobs::ComputeEmptyParts(parts, zx::jobs::ComputeChunkCount(count, chunk));
	std::atomic<int> shownAll(0);
	auto check = [&](int lo, int hi, int)
	{
		std::vector<const ActorRecord *> &into = parts[lo / chunk].items;
		int shownHere = 0;
		for (int i = lo; i < hi; i++)
		{
			const ActorRecord *thing = things[i];
			const int at = zx::gamecopy::IndexOf(thing);
			const int t = at >= 0 && at < (int)g_ticPieceOf.size() ? g_ticPieceOf[at] : -1;
			if (t == -2) continue;   // [Doruk-Aksoy] drawn by no view this tic
			if (t >= 0 && t < hiddenCount && t < ticCount && TicPieceHolds(thing, tic[t])) { hidden[t] = 0; shownHere++; }
			else into.push_back(thing);
		}
		shownAll.fetch_add(shownHere, std::memory_order_relaxed);
	};
	if (count > 0) pool.ParallelChunks(count, chunk, check);
	static std::vector<int> starts;
	zx::jobs::ComputeJoinParts(parts, frame, starts);
	const int shown = shownAll.load();
	g_lastShown = shown;
	g_lastFrameDerived = (int)frame.size();
	ZX_COUNT("ticstream/tic_shown", "sprites", shown);
	ZX_COUNT("ticstream/frame_derived", "actors", (double)frame.size());
	if (!frame.empty()) BuildSprites(&frame[0], (int)frame.size());
}

// [Doruk-Aksoy] A particle the walk reached, waiting for BuildQueuedParticles.
struct QueuedParticle
{
	particle_t *particle;
	const void *who;
	sector_t *sector;
};

static std::vector<QueuedParticle> g_queuedParticles;
static std::vector<SpritePiece> g_particlePieces;   // [Doruk-Aksoy] one a queued particle, and only grows, as a sprite chunk's pieces do
static std::vector<unsigned char> g_particleDrawn;
// [Doruk-Aksoy] Particles a lane derives at a time.
static const int kParticleChunk = 64;

void QueueParticle(particle_t *particle, const void *who, sector_t *sector)
{
	const QueuedParticle q = { particle, who, sector };
	g_queuedParticles.push_back(q);
}

void BuildQueuedParticles()
{
	const int n = (int)g_queuedParticles.size();
	if (n == 0) return;
	ZX_ZONE("scene/particles");   // [Doruk-Aksoy] the particles the walk reached derived and appended
	ParticleLook look;
	if (ParticleLookOf(look))
	{
		if ((int)g_particlePieces.size() < n) g_particlePieces.resize(n);
		g_particleDrawn.resize(n);
		auto derive = [&look](int lo, int hi, int)
		{
			for (int i = lo; i < hi; i++)
				g_particleDrawn[i] = DeriveParticle(g_queuedParticles[i].particle, g_queuedParticles[i].sector, look, g_particlePieces[i]) ? 1 : 0;
		};
		zx::jobs::Pool().ParallelChunks(n, kParticleChunk, derive);
		const bool capture = zx::levelmesh::DynamicCaptureActive();
		static std::vector<PlaceItem> run;
		run.clear();
		for (int i = 0; i < n; i++)
		{
			if (!g_particleDrawn[i]) continue;
			const SpritePiece &p = g_particlePieces[i];
			if (fua_sprite_check) Note(g_ours, g_queuedParticles[i].who, true, p.piece, p.quad, 4);
			if (capture) run.push_back(PlaceItem{ &p, NULL, -1, NULL, 0, 0, -1, 0, 0, 0 });
		}
		PlaceItems(run);
		for (size_t i = 0; i < run.size(); i++) zx::levelmesh::CountParticle();
	}
	g_queuedParticles.clear();
}

}} // namespace zx::sprites

DEV_MODE_CCMD( fua_sprite_report )
{
	const zx::frame::OracleTally &t = zx::sprites::g_check;
	Printf( "sprites: %d actors derived beside GL (%d model surfaces), %d differed (%d only ours, %d only GL's)\n%s",
		t.checked, zx::sprites::g_checkedModelSurfaces, t.differed, zx::sprites::g_onlyOurs, zx::sprites::g_onlyGL, t.firsts.c_str( ) );
	zx::frame::ComputeOracleReset( zx::sprites::g_check );
	zx::sprites::g_onlyOurs = zx::sprites::g_onlyGL = 0;
	zx::sprites::g_checkedModelSurfaces = 0;
}

namespace zx { namespace sprites {

// [rc4l] The model capture's pieces reach fua_sprite_check through its listener, since the capture is below the sprites.
static void HearModelPiece(const void *who, bool fromGL, const MeshPiece &piece, const MeshVertex *verts, int count)
{
	if (fromGL) NoteGLSprite(who, false, piece, verts, count);
	else NoteDerivedSprite(who, piece, verts, count);
}
static const bool s_hearingModels = (zx::levelmesh::SetModelPieceListener(&HearModelPiece), true);

}} // namespace zx::sprites
