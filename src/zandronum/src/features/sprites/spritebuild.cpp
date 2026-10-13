// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/spritebuild.h"
#include "features/levelmesh/levelmesh.h"   // [rc4l] LightListBottom
#include "features/hwrender/computation/lighttouch_compute.h"   // [rc4l] ComputeDynLightColor
#include "features/frame/computation/oracle_compute.h"   // [rc4l] OracleTally
#include "features/jobs/jobs.h"   // [rc4l] the pool BuildSprites derives on

#include "features/sprites/computation/spritestyle_compute.h"
#include "features/draw2d/computation/quad2d_compute.h"   // [rc4l] ComputeTextureMode
#include "features/hwrender/computation/fuzz_compute.h"   // [rc4l] ComputeFuzzKind
#include "features/sprites/computation/spritequad_compute.h"
#include "features/sprites/computation/spritelight_compute.h"
#include "features/sprites/computation/spriteclip_compute.h"
#include "features/sprites/spriteframe.h"   // [rc4l] gl_GetSpriteFrame
#include "features/damage-tint/damagetint.h"   // [rc4l] DamageTint_SpriteGradient
#include "features/sprites/computation/spriteframe_compute.h"   // [rc4l] ComputeSpriteOnGameThread
#include "features/sprites/computation/modelpose_compute.h"
#include "features/sprites/modelbuild.h"
#include "features/sprite-roll/computation/spriteroll_compute.h"
#include "features/levelmesh/staticmesh.h"
#include "features/levelmesh/flatmesh.h"            // [rc4l] the walk the stream's pieces belong to
#include "features/levelmesh/captureshading.h"
#include "features/levelmesh/modelmesh.h"           // [rc4l] a derived model's capture
#include "features/levelmesh/computation/flatmesh_compute.h"
#include "features/hwrender/spriteview.h"
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
#include "features/dynlights/dynlight.h"   // [rc4l] ADynamicLight, FLightNode

#include <string.h>
#include <map>
#include <set>
#include <tuple>
#include <vector>

EXTERN_CVAR(Int, gl_spriteclip)
EXTERN_CVAR(Float, gl_sclipfactor)
EXTERN_CVAR(Float, gl_sclipthreshold)
EXTERN_CVAR(Int, gl_fuzztype)
EXTERN_CVAR(Bool, gl_sprite_blend)
EXTERN_CVAR(Bool, gl_usecolorblending)
EXTERN_CVAR(Bool, gl_enhanced_nightvision)
EXTERN_CVAR(Int, gl_enhanced_nv_stealth)
EXTERN_CVAR(Int, gl_billboard_mode)
EXTERN_CVAR(Bool, gl_lights)
EXTERN_CVAR(Bool, gl_light_sprites)
EXTERN_CVAR(Bool, gl_light_particles)
EXTERN_CVAR(Int, gl_particles_style)
EXTERN_CVAR(Bool, gl_billboard_particles)
EXTERN_CVAR(Float, gl_lights_size)
EXTERN_CVAR(Float, gl_lights_intensity)
EXTERN_CVAR(Float, transsouls)

// [rc4l] Derive every sprite beside GL's and compare them bit for bit, counting what differs for fua_sprite_report.
CVAR(Bool, fua_sprite_check, false, 0)

using zx::levelmesh::MeshPiece;
using zx::levelmesh::MeshVertex;

static_assert(STYLEOP_Add == zx::sprites::kOpAdd && STYLEOP_Sub == zx::sprites::kOpSub && STYLEOP_Fuzz == zx::sprites::kOpFuzz &&
              STYLEOP_Shadow == zx::sprites::kOpShadow, "ERenderOp must match spritestyle_compute.h");
static_assert(STYLEALPHA_Zero == zx::sprites::kAlphaZero && STYLEALPHA_One == zx::sprites::kAlphaOne &&
              STYLEALPHA_Src == zx::sprites::kAlphaSrc && STYLEALPHA_InvSrc == zx::sprites::kAlphaInvSrc, "ERenderAlpha must match");
static_assert(STYLEF_TransSoulsAlpha == zx::sprites::kFlagTransSouls && STYLEF_Alpha1 == zx::sprites::kFlagAlpha1 &&
              STYLEF_RedIsAlpha == zx::sprites::kFlagRedIsAlpha && STYLEF_FadeToBlack == zx::sprites::kFlagFadeToBlack, "ERenderFlags must match");

namespace zx { namespace sprites {

struct SpritePiece
{
	MeshPiece  piece;
	MeshVertex quad[4];
};

static const int kMaxBands = 16;

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

bool SpritesTakeDynamicLight()
{
	return gl_lights && zx::hwrender::SpriteView().DynamicLightCount() && !gl_fixedcolormap && gl_light_sprites;
}

// [rc4l] The lower edge of each light list entry at the actor, as SplitSprite and SetSpriteColor measure it.
static int LightListBottoms(const AActor *actor, const sector_t *sector, float *bottoms, int max)
{
	const TArray<lightlist_t> &lightlist = actor->Sector->e->XFloor.lightlist;
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

// [rc4l] gl_SetDynSpriteLight: the dynamic light at a point in a subsector, but not from a light `self` carries that does not light it.
static void PointLight(fixed_t x, fixed_t y, fixed_t z, subsector_t *subsec, const AActor *self, float out[3])
{
	for (FLightNode *node = subsec->lighthead; node; node = node->nextLight)
	{
		ADynamicLight *light = node->lightsource;
		if ((light->flags2 & MF2_DORMANT) || ((light->flags4 & MF4_DONTLIGHTSELF) && light->target == self)) continue;
		const float dist = FVector3(FIXED2FLOAT(x - light->x), FIXED2FLOAT(y - light->y), FIXED2FLOAT(z - light->z)).Length();
		const float radius = light->GetRadius() * gl_lights_size;
		// [rc4l] gl_SetDynSpriteLight takes the light's colour without the additive scaling a surface's light has.
		float c[3];
		zx::hwrender::ComputeDynLightColor(light->GetRed(), light->GetGreen(), light->GetBlue(), false, gl_lights_intensity, c);
		const float bright = light->IsSubtractive() ? (float)FVector3(c[0], c[1], c[2]).Length() : 0.f;
		ComputeSpriteLightTerm(dist, radius, c[0], c[1], c[2], light->IsSubtractive(), bright, out);
	}
}

// [rc4l] gl_SetDynSpriteLight for an actor, summed the way GL sums it.
static void ActorLight(const AActor *self, float out[3])
{
	PointLight(self->x, self->y, self->z + (self->height >> 1), self->subsector, self, out);
}

void ActorDynamicLight(const AActor *actor, float out[3])
{
	out[0] = out[1] = out[2] = 0.f;
	ActorLight(actor, out);
}

void PointDynamicLight(fixed_t x, fixed_t y, fixed_t z, subsector_t *subsec, float out[3])
{
	out[0] = out[1] = out[2] = 0.f;
	if (subsec != NULL) PointLight(x, y, z, subsec, NULL, out);
}

// [rc4l] GLSprite's ThingColor, which GL's shader multiplies the sprite by as the object colour, folded into the shading.
static void ApplyThingColor(PalEntry color, MeshPiece &mp)
{
	mp.colorR *= color.r / 255.f;
	mp.colorG *= color.g / 255.f;
	mp.colorB *= color.b / 255.f;
}

// [rc4l] An actor as GLSprite::Process first takes it: whether it is drawn from here at all, where, and the model it is drawn as, if any.
struct Subject
{
	int spritenum;
	fixed_t scaleX, scaleY;
	fixed_t frac, thingx, thingy, thingz;
	sector_t *rendersector;
	float x, y, z;
	FSpriteModelFrame *model;
};

static bool SubjectOf(AActor *thing, Subject &s)
{
	const zx::hwrender::SpriteViewProvider &view = zx::hwrender::SpriteView();
	if (thing == view.ViewActor()) return false;
	if (thing == NULL || thing->sprite == 0 || !thing->IsVisibleToPlayer()) return false;

	s.spritenum = thing->sprite;
	s.scaleX = thing->scaleX; s.scaleY = thing->scaleY;
	if (thing->player != NULL) P_CheckPlayerSprite(thing, s.spritenum, s.scaleX, s.scaleY);

	if (thing->renderflags & RF_INVISIBLE || !thing->RenderStyle.IsVisible(thing->alpha))
	{
		if (!(thing->flags & MF_STEALTH) || !gl_fixedcolormap || !gl_enhanced_nightvision) return false;
	}
	if (!(currentmapsection[thing->subsector->mapsection >> 3] & (1 << (thing->subsector->mapsection & 7)))) return false;
	if (!view.PortalIsMirror() && !(players[consoleplayer].cheats & CF_CHASECAM) && players[consoleplayer].camera != nullptr &&
	    players[consoleplayer].camera->player != nullptr && thing == players[consoleplayer].camera->player->pIcon) return false;

	s.frac = C_ShouldForceInterpolation() ? FRACUNIT : r_TicFrac;
	s.thingx = thing->PrevX + FixedMul(s.frac, thing->x - thing->PrevX);
	s.thingy = thing->PrevY + FixedMul(s.frac, thing->y - thing->PrevY);
	s.thingz = thing->PrevZ + FixedMul(s.frac, thing->z - thing->PrevZ);

	// [rc4l] Too close to the camera to look like anything, unless it only moves up and down or is a model.
	if (P_AproxDistance(s.thingx - viewx, s.thingy - viewy) < 2 * FRACUNIT && !(thing->velx == 0 && thing->vely == 0 && thing->velz != 0) &&
	    !gl_FindModelFrame(RUNTIME_TYPE(thing), s.spritenum, thing->frame, false)) return false;
	AActor *const viewActor = view.ViewActor();
	if (thing->flags & MF_MISSILE && thing->target == viewActor && viewActor != NULL &&
	    P_AproxDistance(s.thingx - viewx, s.thingy - viewy) < thing->Speed) return false;
	if (view.PortalRejectsPoint(s.thingx, s.thingy)) return false;

	// [rc4l] The sweep hands each actor its own sector, so GL never fakes the flat here.
	s.rendersector = thing->Sector;
	s.x = FIXED2FLOAT(s.thingx);
	s.z = FIXED2FLOAT(s.thingz - thing->floorclip);
	s.y = FIXED2FLOAT(s.thingy);
	if (thing->flags2 & MF2_FLOATBOB) s.z += FIXED2FLOAT(thing->GetBobOffset(r_TicFrac));
	s.model = gl_FindModelFrame(RUNTIME_TYPE(thing), s.spritenum, thing->frame, !!(thing->flags & MF_DROPPED));
	return true;
}

// [rc4l] The light, colormap and style Process gives an actor, false when it is fully transparent; a model has no frame that could turn fullbright off or make it transparent.
struct Look
{
	bool fullbright;
	int lightlevel;
	FColormap cm;
	SpriteStyleOut so;
	bool inverse;      // [rc4l] drawn in TM_INVERSE, as gl_GetRenderStyle takes InvertSource
	bool colorBlend;   // [rc4l] blended by its own colour (ComputeColorBlending)
};

static bool LookOf(AActor *thing, const Subject &s, bool disableFullbright, bool textureTransparent, Look &l)
{
	// [rc4l] Fullbright unless GLDEFS turns it off for this frame, the sky-aware sector light, and a glowing floor.
	l.fullbright = (thing->flags5 & MF5_BRIGHT) || ((thing->renderflags & RF_FULLBRIGHT) && !disableFullbright);
	sector_t *rendersector = s.rendersector;
	l.lightlevel = l.fullbright ? 255 : gl_ClampLight(rendersector->GetTexture(sector_t::ceiling) == skyflatnum ?
		rendersector->GetCeilingLight() : rendersector->GetFloorLight());
	{
		FTexture *floortex = TexMan[rendersector->GetTexture(sector_t::floor)];
		if (floortex != NULL && floortex->isGlowing())
			l.lightlevel = ComputeSpriteGlow(l.lightlevel, (s.thingz - rendersector->floorplane.ZatPoint(s.thingx, s.thingy)).Raw(),
				floortex->gl_info.GlowHeight);
		l.lightlevel = (BYTE)l.lightlevel;
	}

	FRenderStyle style = thing->RenderStyle;
	bool enhancedvision = false;
	if (gl_fixedcolormap)
	{
		if ((gl_enhanced_nv_stealth > 0 && gl_fixedcolormap == CM_LITE) || (gl_enhanced_nv_stealth == 2 && gl_fixedcolormap >= CM_TORCH) ||
		    (gl_enhanced_nv_stealth == 3)) enhancedvision = true;
		l.cm.Clear();
		if (gl_fixedcolormap == CM_LITE && gl_enhanced_nightvision &&
		    (thing->IsKindOf(RUNTIME_CLASS(AInventory)) || thing->flags3 & MF3_ISMONSTER || thing->flags & MF_MISSILE || thing->flags & MF_CORPSE))
			style.Flags |= STYLEF_InvertSource;
	}
	else
	{
		l.cm = rendersector->ColorMap;
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
	si.fuzzType = gl_fuzztype;
	si.spriteBlend = gl_sprite_blend;
	si.model = s.model != NULL;
	si.textureTransparent = textureTransparent;
	si.enhancedVision = enhancedvision && gl_enhanced_nightvision;
	si.stealth = !!(thing->flags & MF_STEALTH);
	si.translucent = StyleOf(LegacyRenderStyles[STYLE_Translucent]);
	l.so = ComputeSpriteStyle(si);
	l.inverse = zx::draw2d::ComputeTextureMode(l.so.style.flags, false) == zx::draw2d::kTexInverse;
	const ColorBlendIn cb = { gl_usecolorblending, gl_fixedcolormap != CM_DEFAULT, s.model == NULL, l.fullbright, textureTransparent };
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

// [rc4l] GLSprite::Draw's dynamic light for an actor: its lights, unless it is a shadow, fullbright or under a fixed colormap.
static void DrawLight(AActor *thing, const Look &l, float dyn[3])
{
	dyn[0] = dyn[1] = dyn[2] = 0.f;
	if (l.so.style.blendOp != kOpShadow && !l.fullbright && SpritesTakeDynamicLight()) ActorLight(thing, dyn);
}

// [rc4l] GL's ThingColor: a stencil actor's fill, else white.
static PalEntry ThingColorOf(const AActor *thing)
{
	return (thing->RenderStyle.Flags & STYLEF_ColorIsFixed) ? PalEntry(thing->fillcolor) : PalEntry(0xffffff);
}

// [rc4l] A model as Process, Draw and gl_RenderModel take it: one band, lit from the light list entry at the actor's middle, its surfaces captured under the pose's matrix.
static void DeriveModel(AActor *thing, const Subject &s, const Look &l, bool checked)
{
	int lightlevel = l.lightlevel;
	FColormap cm = l.cm;
	if (!gl_fixedcolormap && !l.fullbright)
	{
		float bottoms[kMaxBands];
		const int n = LightListBottoms(thing, thing->Sector, bottoms, kMaxBands);
		// [rc4l] SetSpriteColor, against a model's single height and the actor's y as GL measures it.
		const int entry = ComputeSpriteColorEntry(s.z, float(thing->y + (thing->height >> 1)), bottoms, n);
		if (entry >= 0) TakeListLight(thing->Sector->e->XFloor.lightlist[entry], false, lightlevel, cm);
	}
	DrawFade(l.so, lightlevel, cm);
	float dyn[3];
	DrawLight(thing, l, dyn);
	MeshPiece look;
	zx::levelmesh::ModelPieceLook(lightlevel, l.fullbright ? 0 : getExtraLight(), cm, l.fullbright, dyn, ThingColorOf(thing).d, l.so.trans,
		ComputeSpriteBlendMode(l.so.style.blendOp, l.so.style.destAlpha, l.so.trans), s.x, s.y, s.z, look);
	look.inverse = l.inverse;

	ModelPose pose;
	ModelPoseFor(thing, s.model, s.x, s.y, s.z, pose);
	float modelToWorld[16];
	ComputeModelMatrix(pose, modelToWorld);
	zx::levelmesh::ModelCaptureBegin(look, modelToWorld, thing, checked ? zx::levelmesh::kModelChecked : zx::levelmesh::kModelDraw);
	CaptureModelFrames(s.model, thing->state, thing->tics, RUNTIME_TYPE(thing), (s.model->flags & MDL_IGNORETRANSLATION) ? 0 : int(thing->Translation));
	zx::levelmesh::ModelCaptureEnd();
}

// [rc4l] Derive an actor's sprite as GLSprite::Process and Draw do: the pieces drawn, 0 for none; on a worker thread (`lane`), -1 when its material is not made yet, which only the game thread may do.
static int DeriveSprite(AActor *thing, const Subject &s, SpritePiece *out, int max, bool lane = false)
{
	const zx::hwrender::SpriteViewProvider &view = zx::hwrender::SpriteView();
	const int spritenum = s.spritenum;
	const fixed_t scaleX = s.scaleX, scaleY = s.scaleY, thingx = s.thingx, thingy = s.thingy, frac = s.frac;
	const float x = s.x, y = s.y, z = s.z;

	// [rc4l] The rotation the viewer sees, as GL picks it.
	bool mirror = false;
	const FTextureID patch = gl_GetSpriteFrame(spritenum, thing->frame, -1, R_PointToAngle(thingx, thingy) - thing->angle, &mirror);
	if (!patch.isValid()) return 0;
	const int type = thing->renderflags & RF_SPRITETYPEMASK;
	// [rc4l] A floor sprite leaves GL's quad unset, so it is not drawn here either.
	if (type != RF_FACESPRITE && type != RF_WALLSPRITE) return 0;
	zx::materials::MaterialRef material = NULL;
	if (!lane) material = zx::materials::MaterialForTexture(TexMan[patch], type == RF_FACESPRITE);
	else if (!zx::materials::MaterialIfMade(TexMan[patch], type == RF_FACESPRITE, material)) return -1;
	zx::materials::SpriteFrameInfo fi;
	if (!zx::materials::MaterialSpriteFrame(material, fi)) return 0;

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

	// [rc4l] GL scales the already scaled height once more here.
	const float spriteheight = FIXED2FLOAT(scaleY) * r.height;
	const bool living = (thing->player || thing->flags3 & MF3_ISMONSTER || thing->IsKindOf(RUNTIME_CLASS(AInventory))) &&
		(thing->flags & MF_ICECORPSE || !(thing->flags & MF_CORPSE));
	if (spriteheight > 0 && gl_spriteclip > 0 && !(thing->flags2 & MF2_FLOATBOB) && type == RF_FACESPRITE &&
	    ComputeSpriteClips(living, gl_spriteclip))
	{
		// [rc4l] The floor and ceiling GL clips against: a 3D floor the actor stands on, a height transfer, or the sector's own.
		float btm = 1000000.0f, top = -1000000.0f;
		extsector_t::xfloor &xf = thing->Sector->e->XFloor;
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
		else if (thing->Sector->heightsec && !(thing->Sector->heightsec->MoreFlags & SECF_IGNOREHEIGHTSEC))
		{
			if (thing->flags2 & MF2_ONMOBJ && thing->floorz == thing->Sector->heightsec->floorplane.ZatPoint(thingx, thingy))
			{
				btm = FIXED2FLOAT(thing->floorz);
				top = FIXED2FLOAT(thing->ceilingz);
			}
		}
		if (btm == 1000000.0f) btm = FIXED2FLOAT(thing->Sector->floorplane.ZatPoint(thingx, thingy) - thing->floorclip);
		if (top == -1000000.0f) top = FIXED2FLOAT(thing->Sector->ceilingplane.ZatPoint(thingx, thingy));
		SpriteClipIn ci;
		ci.living = living;
		ci.monster = !!(thing->flags3 & MF3_ISMONSTER);
		ci.noGravity = !!(thing->flags & MF_NOGRAVITY);
		ci.corpse = !!(thing->flags & MF_CORPSE);
		ci.spriteClip = gl_spriteclip;
		ci.clipFactor = (float)gl_sclipfactor;
		ci.clipThreshold = (float)gl_sclipthreshold;
		ci.bottom = btm;
		ci.top = top;
		ci.spriteHeight = spriteheight;
		ComputeSpriteClip(ci, e.z1, e.z2);
	}

	Look look;
	if (!LookOf(thing, s, fi.disableFullbright, fi.transparent, look)) return 0;
	const bool fullbright = look.fullbright;
	const int lightlevel = look.lightlevel;
	const FColormap cm = look.cm;
	const SpriteStyleOut so = look.so;

	// [rc4l] Bands: one, unless a 3D floor's light list cuts the sprite; an XY billboard takes the band holding its centre instead.
	struct Band { float z1, z2, vt, vb; int lightlevel; FColormap cm; };
	Band bands[kMaxBands];
	int nbands = 0;
	const TArray<lightlist_t> &lightlist = thing->Sector->e->XFloor.lightlist;
	const bool xyProcess = !(thing->renderflags & RF_FORCEYBILLBOARD) && type == RF_FACESPRITE && players[consoleplayer].camera &&
		(gl_billboard_mode == 1 || thing->renderflags & RF_FORCEXYBILLBOARD);
	if (xyProcess || lightlist.Size() == 0 || gl_fixedcolormap || fullbright)
	{
		Band &b = bands[nbands++];
		b.z1 = e.z1; b.z2 = e.z2; b.vt = fi.vt; b.vb = fi.vb; b.lightlevel = lightlevel; b.cm = cm;
		if (xyProcess && !gl_fixedcolormap && !fullbright)
		{
			float bottoms[kMaxBands];
			const int n = LightListBottoms(thing, thing->Sector, bottoms, kMaxBands);
			// [rc4l] GL measures from the actor's y, not its z, and in raw 16.16 units; kept so the two agree.
			const int entry = ComputeSpriteColorEntry(e.z2, float(thing->y + (thing->height >> 1)), bottoms, n);
			if (entry >= 0) TakeListLight(lightlist[entry], false, b.lightlevel, b.cm);
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
			TakeListLight(lightlist[split[i].entry], true, b.lightlevel, b.cm);
		}
	}

	// [rc4l] Draw: the actor's dynamic light, and the corners an XY billboard or a roll turns.
	float dyn[3];
	DrawLight(thing, look, dyn);
	const bool xyDraw = !(thing->renderflags & RF_FORCEYBILLBOARD) && (gl_billboard_mode == 1 || thing->renderflags & RF_FORCEXYBILLBOARD);
	const bool roll = (thing->renderflags & RF_ROLLSPRITE) != 0;
	float viewYaw = 0.f, viewPitch = 0.f;
	if (xyDraw || roll) view.ViewAngles(viewYaw, viewPitch);
	const float rollDegrees = roll ? zx::ComputeSpriteRollDegrees(thing->PrevRoll, thing->roll, frac.Raw()) : 0.f;
	const int rel = fullbright ? 0 : getExtraLight();

	int drawn = 0;
	for (int i = 0; i < nbands && drawn < max; i++)
	{
		Band &b = bands[i];
		DrawFade(so, b.lightlevel, b.cm);

		SpriteEdges be = e;
		be.z1 = b.z1; be.z2 = b.z2;
		float c[4][3];
		ComputeSpriteCorners(be, xyDraw, roll, viewYaw, viewPitch, rollDegrees, c);

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
		mp.mirrorLine = zx::levelmesh::MirrorCaptureLine();
		mp.stackSlot = zx::levelmesh::StackCaptureSlot();
		mp.dynLightIndex = -1;
		mp.lightLevel = b.lightlevel;
		mp.lightColor = b.cm.LightColor.d;
		mp.fadeColor = b.cm.FadeColor.d;
		zx::levelmesh::CaptureShading(b.lightlevel, rel, b.cm, mp);
		ApplyThingColor(ThingColorOf(thing), mp);
		mp.stencil = (so.style.flags & STYLEF_ColorIsFixed) != 0;
		mp.alphaTest = so.hw != kHwNoAlphaTest;
		mp.inverse = look.inverse;
		mp.colorBlend = look.colorBlend;
		mp.fuzz = zx::hwrender::ComputeFuzzKind(gl_fuzztype, so.overrideShader >= 0);
		// [rc4l] GLSprite::Draw's damage tint, over this band's own texture span as GL takes each band's.
		DamageTint_SpriteGradient(thing, so.style.blendOp, so.style.flags, b.vt, b.vb, mp.tint);
		if (!fullbright) { mp.dynR = dyn[0]; mp.dynG = dyn[1]; mp.dynB = dyn[2]; }
		mp.cpuLight = true;
		mp.normX = mp.normY = mp.normZ = 0.f;
		mp.translation = thing->Translation;
		mp.alpha = so.trans;
		mp.blendMode = ComputeSpriteBlendMode(so.style.blendOp, so.style.destAlpha, so.trans);
		mp.sortX = x; mp.sortY = y; mp.sortZ = z;
		mp.quad = true;
	}
	return drawn;
}

// [rc4l] Derive a particle as GLSprite::ProcessParticle and Draw do: false when it draws nothing, as an untextured square does not reach the backend.
static bool DeriveParticle(particle_t *particle, sector_t *sector, SpritePiece &p)
{
	const zx::hwrender::SpriteViewProvider &view = zx::hwrender::SpriteView();
	if (view.PortalRejectsPoint(particle->x, particle->y)) return false;
	if (particle->trans == 0) return false;
	// [rc4l] Only a round or smooth particle has a texture, and the backend draws no sprite without one.
	FTexture *lump = gl_particles_style ? zx::materials::ParticleTexture(gl_particles_style) : NULL;
	const void *material = lump != NULL ? zx::materials::MaterialForTexture(lump, true) : NULL;
	if (material == NULL) return false;

	int lightlevel = gl_ClampLight(sector->GetTexture(sector_t::ceiling) == skyflatnum ? sector->GetCeilingLight() : sector->GetFloorLight());
	FColormap cm;
	if (gl_fixedcolormap) cm.Clear();
	else if (!particle->bright)
	{
		const TArray<lightlist_t> &lightlist = sector->e->XFloor.lightlist;
		cm = sector->ColorMap;
		for (unsigned i = 0; i < lightlist.Size(); i++)
		{
			const int lightbottom = i < lightlist.Size() - 1 ? (int)(lightlist[i + 1].plane.ZatPoint(particle->x, particle->y))
				: (int)(sector->floorplane.ZatPoint(particle->x, particle->y));
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
		cm = sector->ColorMap;
		cm.ClearColor();
	}
	const float trans = particle->trans / 255.0f;
	PalEntry color = particle->color;
	color.a = 255;
	float ul = 0.f, vt = 0.f, ur = 1.f, vb = 1.f;
	zx::materials::MaterialCorners(material, ul, vt, ur, vb);

	const float x = FIXED2FLOAT(particle->x), y = FIXED2FLOAT(particle->y), z = FIXED2FLOAT(particle->z);
	float scalefac = particle->size / 4.0f;
	// [BB] The smooth particles are smaller than the other ones.
	if (gl_particles_style == 2) scalefac *= 1.7;
	float vx, vy;
	view.ViewVector(vx, vy);
	const SpriteEdges e = { x + vy * scalefac, y - vx * scalefac, x - vy * scalefac, y + vx * scalefac, z - scalefac, z + scalefac };
	float viewYaw = 0.f, viewPitch = 0.f;
	if (gl_billboard_particles) view.ViewAngles(viewYaw, viewPitch);
	float c[4][3];
	ComputeSpriteCorners(e, gl_billboard_particles, false, viewYaw, viewPitch, 0.f, c);

	p.piece = MeshPiece();
	p.quad[0].Set(c[0][0], c[0][1], c[0][2], ul, vt);
	p.quad[1].Set(c[1][0], c[1][1], c[1][2], ur, vt);
	p.quad[2].Set(c[2][0], c[2][1], c[2][2], ul, vb);
	p.quad[3].Set(c[3][0], c[3][1], c[3][2], ur, vb);
	MeshPiece &mp = p.piece;
	mp.range.offset = 0;
	mp.range.count = 0;
	mp.material = material;
	mp.mirrorLine = zx::levelmesh::MirrorCaptureLine();
	mp.stackSlot = zx::levelmesh::StackCaptureSlot();
	mp.dynLightIndex = -1;
	mp.lightLevel = lightlevel;
	mp.lightColor = cm.LightColor.d;
	mp.fadeColor = cm.FadeColor.d;
	zx::levelmesh::CaptureShading(lightlevel, getExtraLight(), cm, mp);
	ApplyThingColor(color, mp);
	float dyn[3] = { 0.f, 0.f, 0.f };
	if (gl_lights && view.DynamicLightCount() && !gl_fixedcolormap && gl_light_particles)
		PointLight(particle->x, particle->y, particle->z, particle->subsector, NULL, dyn);
	mp.dynR = dyn[0]; mp.dynG = dyn[1]; mp.dynB = dyn[2];
	mp.cpuLight = true;
	mp.normX = mp.normY = mp.normZ = 0.f;
	mp.translation = 0;
	mp.alpha = trans;
	// [rc4l] STYLE_Translucent: blended unless it is fully opaque.
	mp.blendMode = trans < 1.f - 1.f / 256.f ? 1 : 0;
	mp.alphaTest = ComputeParticleAlphaTested(gl_particles_style, trans);
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
	if (x.mirrorLine != y.mirrorLine || x.stackSlot != y.stackSlot) return "walk";
	if (x.solid != y.solid) return "solid";
	if (x.stencil != y.stencil) return "stencil";
	if (x.alphaTest != y.alphaTest) return "alpha test";
	return NULL;
}

void CheckFrameSprites()
{
	if (!fua_sprite_check) { g_gl.clear(); g_ours.clear(); return; }
	// [rc4l] Both lists are in actor order within an actor and a walk; match each actor's run of pieces in one walk against the other's in the same walk, so a mirror's view of an actor is not held against the player's.
	typedef std::tuple<const void *, int, int> RunKey;
	const auto keyOf = [](const Noted &n) { return RunKey(n.who, n.piece.mirrorLine, n.piece.stackSlot); };
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
				zx::frame::ComputeOracleNote(g_check, "  %s at (%.0f, %.0f): %s; GL corner %.9g %.9g %.9g, ours %.9g %.9g %.9g\n",
					part ? "particle" : ((const AActor *)a)->GetClass()->TypeName.GetChars(),
					FIXED2FLOAT(part ? ((const particle_t *)a)->x : ((const AActor *)a)->x),
					FIXED2FLOAT(part ? ((const particle_t *)a)->y : ((const AActor *)a)->y), why,
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

void BuildSprite(AActor *thing)
{
	Subject s;
	if (!SubjectOf(thing, s)) return;
	if (s.model != NULL)
	{
		Look l;
		if (LookOf(thing, s, false, false, l)) DeriveModel(thing, s, l, fua_sprite_check);
		return;
	}
	// [rc4l] Kept between actors: constructing sixteen records an actor cost more than deriving it.
	static SpritePiece pieces[kMaxBands];
	const int n = DeriveSprite(thing, s, pieces, kMaxBands);
	if (fua_sprite_check)
		for (int i = 0; i < n; i++) Note(g_ours, thing, false, pieces[i].piece, pieces[i].quad, 4);
	if (!zx::levelmesh::DynamicCaptureActive()) return;
	for (int i = 0; i < n; i++) zx::levelmesh::DynAppend(pieces[i].quad, 4, pieces[i].piece);
}

// [rc4l] An actor a lane derived: its pieces in the lane's list, or that the game thread derives it.
struct LaneSprite
{
	int first, count;
	bool onGameThread;
};

// [rc4l] One chunk of a batch's actors, derived on a lane.
struct SpriteChunk
{
	std::vector<SpritePiece> pieces;
	std::vector<LaneSprite> sprites;
};

static std::vector<SpriteChunk> g_spriteChunks;
// [rc4l] Actors a lane derives at a time: enough that a chunk outweighs taking it, few enough to spread a frame's actors.
static const int kSpriteChunk = 32;

static void DeriveChunk(AActor *const *things, int count, int chunk)
{
	SpriteChunk &c = g_spriteChunks[chunk];
	c.pieces.clear();
	c.sprites.clear();
	const int end = (chunk + 1) * kSpriteChunk < count ? (chunk + 1) * kSpriteChunk : count;
	for (int i = chunk * kSpriteChunk; i < end; i++)
	{
		LaneSprite ls = { (int)c.pieces.size(), 0, false };
		Subject s;
		if (SubjectOf(things[i], s))
		{
			if (ComputeSpriteOnGameThread(s.model != NULL, things[i]->player != NULL)) ls.onGameThread = true;
			else
			{
				c.pieces.resize(ls.first + kMaxBands);
				const int n = DeriveSprite(things[i], s, &c.pieces[ls.first], kMaxBands, true);
				ls.onGameThread = n < 0;
				ls.count = n < 0 ? 0 : n;
				c.pieces.resize(ls.first + ls.count);
			}
		}
		c.sprites.push_back(ls);
	}
}

void BuildSprites(AActor *const *things, int count)
{
	if (count <= 0) return;
	const int chunks = (count + kSpriteChunk - 1) / kSpriteChunk;
	if ((int)g_spriteChunks.size() < chunks) g_spriteChunks.resize(chunks);
	struct Derive
	{
		AActor *const *things;
		int count;
		void operator()(int chunk, int) const { DeriveChunk(things, count, chunk); }
	} derive = { things, count };
	zx::jobs::Pool().ParallelFor(chunks, derive);
	const bool capture = zx::levelmesh::DynamicCaptureActive();
	for (int c = 0; c < chunks; c++)
	{
		const SpriteChunk &chunk = g_spriteChunks[c];
		for (size_t k = 0; k < chunk.sprites.size(); k++)
		{
			const LaneSprite &ls = chunk.sprites[k];
			if (ls.onGameThread) BuildSprite(things[c * kSpriteChunk + (int)k]);
			else if (capture)
				for (int i = 0; i < ls.count; i++) zx::levelmesh::DynAppend(chunk.pieces[ls.first + i].quad, 4, chunk.pieces[ls.first + i].piece);
		}
	}
}

void BuildParticle(particle_t *particle, sector_t *sector)
{
	SpritePiece p;
	const bool drawn = DeriveParticle(particle, sector, p);
	if (drawn && fua_sprite_check) Note(g_ours, particle, true, p.piece, p.quad, 4);
	if (drawn && zx::levelmesh::DynamicCaptureActive())
	{
		zx::levelmesh::DynAppend(p.quad, 4, p.piece);
		zx::levelmesh::CountParticle();
	}
}

}} // namespace zx::sprites

CCMD( fua_sprite_report )
{
	const zx::frame::OracleTally &t = zx::sprites::g_check;
	Printf( "sprites: %d actors derived beside GL (%d model surfaces), %d differed (%d only ours, %d only GL's)\n%s",
		t.checked, zx::sprites::g_checkedModelSurfaces, t.differed, zx::sprites::g_onlyOurs, zx::sprites::g_onlyGL, t.firsts.c_str( ) );
	zx::frame::ComputeOracleReset( zx::sprites::g_check );
	zx::sprites::g_onlyOurs = zx::sprites::g_onlyGL = 0;
	zx::sprites::g_checkedModelSurfaces = 0;
}
