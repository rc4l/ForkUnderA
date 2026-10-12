// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/sprites/spritebuild.h"

#include "features/sprites/computation/spritestyle_compute.h"
#include "features/sprites/computation/spritequad_compute.h"
#include "features/sprites/computation/spritelight_compute.h"
#include "features/sprites/computation/spriteclip_compute.h"
#include "features/sprites/computation/spriteframe_compute.h"
#include "features/sprite-roll/computation/spriteroll_compute.h"
#include "features/levelmesh/staticmesh.h"
#include "features/levelmesh/flatmesh.h"            // [rc4l] the walk the stream's pieces belong to
#include "features/levelmesh/captureshading.h"
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

// [rc4l] GL's settings and view state the derivation still reads, until the renderer interface supplies them (P5).
#include "gl/renderer/gl_colormap.h"
#include "gl/renderer/gl_lightdata.h"
#include "gl/data/gl_data.h"           // glset, getExtraLight, currentmapsection
#include "gl/models/gl_models.h"       // gl_FindModelFrame
#include "gl/dynlights/gl_dynlight.h"  // ADynamicLight, FLightNode

#include <string.h>

EXTERN_CVAR(Int, gl_spriteclip)
EXTERN_CVAR(Float, gl_sclipfactor)
EXTERN_CVAR(Float, gl_sclipthreshold)
EXTERN_CVAR(Int, gl_fuzztype)
EXTERN_CVAR(Bool, gl_sprite_blend)
EXTERN_CVAR(Bool, gl_enhanced_nightvision)
EXTERN_CVAR(Int, gl_enhanced_nv_stealth)
EXTERN_CVAR(Int, gl_billboard_mode)
EXTERN_CVAR(Bool, gl_lights)
EXTERN_CVAR(Bool, gl_light_sprites)
EXTERN_CVAR(Float, gl_lights_size)
EXTERN_CVAR(Float, gl_lights_intensity)
EXTERN_CVAR(Float, transsouls)

// [rc4l] The backend's sprites derived here rather than by GLSprite, models excepted; fua_sprite_check matched GL on 1.6 million actor-frames before this went on.
CVAR(Bool, fua_sprites_own, true, 0)
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

static zx::sprites::SpriteStyle StyleOf(const FRenderStyle &s)
{
	SpriteStyle o = { s.BlendOp, s.SrcAlpha, s.DestAlpha, s.Flags };
	return o;
}

// [rc4l] The lower edge of each light list entry at the actor, as SplitSprite and SetSpriteColor measure it.
static int LightListBottoms(const AActor *actor, const sector_t *sector, float *bottoms, int max)
{
	const TArray<lightlist_t> &lightlist = actor->Sector->e->XFloor.lightlist;
	int n = 0;
	for (unsigned i = 0; i < lightlist.Size() && n < max; i++)
	{
		const fixed_t bottom = (i < lightlist.Size() - 1) ? lightlist[i + 1].plane.ZatPoint(actor->x, actor->y)
		                                                  : sector->floorplane.ZatPoint(actor->x, actor->y);
		bottoms[n++] = FIXED2FLOAT(bottom);
	}
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

// [rc4l] gl_SetDynSpriteLight for an actor, summed the way GL sums it.
static void ActorLight(const AActor *self, float out[3])
{
	const fixed_t x = self->x, y = self->y, z = self->z + (self->height >> 1);
	for (FLightNode *node = self->subsector->lighthead; node; node = node->nextLight)
	{
		ADynamicLight *light = node->lightsource;
		if ((light->flags2 & MF2_DORMANT) || ((light->flags4 & MF4_DONTLIGHTSELF) && light->target == self)) continue;
		const float dist = FVector3(FIXED2FLOAT(x - light->x), FIXED2FLOAT(y - light->y), FIXED2FLOAT(z - light->z)).Length();
		const float radius = light->GetRadius() * gl_lights_size;
		const float lr = light->GetRed() / 255.0f * gl_lights_intensity;
		const float lg = light->GetGreen() / 255.0f * gl_lights_intensity;
		const float lb = light->GetBlue() / 255.0f * gl_lights_intensity;
		const float bright = light->IsSubtractive() ? (float)FVector3(lr, lg, lb).Length() : 0.f;
		ComputeSpriteLightTerm(dist, radius, lr, lg, lb, light->IsSubtractive(), bright, out);
	}
}

// [rc4l] Derive an actor's sprite as GLSprite::Process, Draw and RegisterSprite do: the pieces drawn, 0 for none, -1 for a model GL must draw.
static int DeriveSprite(AActor *thing, SpritePiece *out, int max)
{
	const zx::hwrender::SpriteViewProvider &view = zx::hwrender::SpriteView();
	if (thing == view.ViewActor()) return 0;
	if (thing == NULL || thing->sprite == 0 || !thing->IsVisibleToPlayer()) return 0;

	int spritenum = thing->sprite;
	fixed_t scaleX = thing->scaleX, scaleY = thing->scaleY;
	if (thing->player != NULL) P_CheckPlayerSprite(thing, spritenum, scaleX, scaleY);

	if (thing->renderflags & RF_INVISIBLE || !thing->RenderStyle.IsVisible(thing->alpha))
	{
		if (!(thing->flags & MF_STEALTH) || !gl_fixedcolormap || !gl_enhanced_nightvision) return 0;
	}
	if (!(currentmapsection[thing->subsector->mapsection >> 3] & (1 << (thing->subsector->mapsection & 7)))) return 0;
	if (!view.PortalIsMirror() && !(players[consoleplayer].cheats & CF_CHASECAM) && players[consoleplayer].camera != nullptr &&
	    players[consoleplayer].camera->player != nullptr && thing == players[consoleplayer].camera->player->pIcon) return 0;

	const fixed_t frac = C_ShouldForceInterpolation() ? FRACUNIT : r_TicFrac;
	const fixed_t thingx = thing->PrevX + FixedMul(frac, thing->x - thing->PrevX);
	const fixed_t thingy = thing->PrevY + FixedMul(frac, thing->y - thing->PrevY);
	const fixed_t thingz = thing->PrevZ + FixedMul(frac, thing->z - thing->PrevZ);

	// [rc4l] Too close to the camera to look like anything, unless it only moves up and down.
	if (P_AproxDistance(thingx - viewx, thingy - viewy) < 2 * FRACUNIT && !(thing->velx == 0 && thing->vely == 0 && thing->velz != 0) &&
	    !gl_FindModelFrame(RUNTIME_TYPE(thing), spritenum, thing->frame, false)) return 0;
	AActor *const viewActor = view.ViewActor();
	if (thing->flags & MF_MISSILE && thing->target == viewActor && viewActor != NULL &&
	    P_AproxDistance(thingx - viewx, thingy - viewy) < thing->Speed) return 0;
	if (view.PortalRejectsPoint(thingx, thingy)) return 0;

	// [rc4l] The sweep hands each actor its own sector, so GL never fakes the flat here.
	sector_t *rendersector = thing->Sector;
	const float x = FIXED2FLOAT(thingx);
	float z = FIXED2FLOAT(thingz - thing->floorclip);
	const float y = FIXED2FLOAT(thingy);
	if (thing->flags2 & MF2_FLOATBOB) z += FIXED2FLOAT(thing->GetBobOffset(r_TicFrac));

	if (gl_FindModelFrame(RUNTIME_TYPE(thing), spritenum, thing->frame, !!(thing->flags & MF_DROPPED))) return -1;

	// [rc4l] The rotation the viewer sees, as gl_GetSpriteFrame picks it.
	spritedef_t *sprdef = &::sprites[spritenum];
	if (thing->frame >= sprdef->numframes) return 0;
	const spriteframe_t *sprframe = &SpriteFrames[sprdef->spriteframes + thing->frame];
	bool mirror = false;
	const int rot = ComputeSpriteRotation(R_PointToAngle(thingx, thingy) - thing->angle,
		sprframe->Texture[0] == sprframe->Texture[1], sprframe->Flip, mirror);
	const FTextureID patch = sprframe->Texture[rot];
	if (!patch.isValid()) return 0;
	const int type = thing->renderflags & RF_SPRITETYPEMASK;
	// [rc4l] A floor sprite leaves GL's quad unset, so it is not drawn here either.
	if (type != RF_FACESPRITE && type != RF_WALLSPRITE) return 0;
	const void *material = zx::materials::MaterialForTexture(TexMan[patch], type == RF_FACESPRITE);
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

	// [rc4l] Light: fullbright unless GLDEFS turns it off for this frame, the sky-aware sector light, and a glowing floor.
	const bool fullbright = (thing->flags5 & MF5_BRIGHT) || ((thing->renderflags & RF_FULLBRIGHT) && !fi.disableFullbright);
	int lightlevel = fullbright ? 255 : gl_ClampLight(rendersector->GetTexture(sector_t::ceiling) == skyflatnum ?
		rendersector->GetCeilingLight() : rendersector->GetFloorLight());
	{
		FTexture *floortex = TexMan[rendersector->GetTexture(sector_t::floor)];
		if (floortex != NULL && floortex->isGlowing())
			lightlevel = ComputeSpriteGlow(lightlevel, (thingz - rendersector->floorplane.ZatPoint(thingx, thingy)).Raw(),
				floortex->gl_info.GlowHeight);
		lightlevel = (BYTE)lightlevel;
	}

	FRenderStyle style = thing->RenderStyle;
	bool enhancedvision = false;
	FColormap cm;
	if (gl_fixedcolormap)
	{
		if ((gl_enhanced_nv_stealth > 0 && gl_fixedcolormap == CM_LITE) || (gl_enhanced_nv_stealth == 2 && gl_fixedcolormap >= CM_TORCH) ||
		    (gl_enhanced_nv_stealth == 3)) enhancedvision = true;
		cm.Clear();
		if (gl_fixedcolormap == CM_LITE && gl_enhanced_nightvision &&
		    (thing->IsKindOf(RUNTIME_CLASS(AInventory)) || thing->flags3 & MF3_ISMONSTER || thing->flags & MF_MISSILE || thing->flags & MF_CORPSE))
			style.Flags |= STYLEF_InvertSource;
	}
	else
	{
		cm = rendersector->ColorMap;
		if (fullbright)
		{
			int cr = cm.LightColor.r, cg = cm.LightColor.g, cb = cm.LightColor.b;
			ComputeFullbrightColor(false, cr, cg, cb);
			cm.LightColor.r = cr; cm.LightColor.g = cg; cm.LightColor.b = cb;
		}
		else if (glset.nocoloredspritelighting) cm.Decolorize();
	}

	if (style.BlendOp >= STYLEOP_Fuzz && style.BlendOp <= STYLEOP_FuzzOrRevSub) style.CheckFuzz();
	SpriteStyleIn si;
	si.style = StyleOf(style);
	si.alpha = FIXED2FLOAT(thing->alpha);
	si.transSouls = transsouls;
	si.fuzzType = gl_fuzztype;
	si.spriteBlend = gl_sprite_blend;
	si.model = false;
	si.textureTransparent = fi.transparent;
	si.enhancedVision = enhancedvision && gl_enhanced_nightvision;
	si.stealth = !!(thing->flags & MF_STEALTH);
	si.translucent = StyleOf(LegacyRenderStyles[STYLE_Translucent]);
	const SpriteStyleOut so = ComputeSpriteStyle(si);
	if (so.trans == 0.0f) return 0;

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
	float dyn[3] = { 0.f, 0.f, 0.f };
	if (so.style.blendOp != kOpShadow && gl_lights && view.DynamicLightCount() && !gl_fixedcolormap && !fullbright && gl_light_sprites)
		ActorLight(thing, dyn);
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
		if (b.cm.FadeColor.a == 0 && so.style.blendOp == kOpShadow && !gl_isBlack(b.cm.FadeColor))
			b.cm.FadeColor.a = ::clamp<int>(255 - b.lightlevel, 60, 255);
		if (so.style.flags & STYLEF_FadeToBlack) b.cm.FadeColor = 0;

		SpriteEdges be = e;
		be.z1 = b.z1; be.z2 = b.z2;
		float c[4][3];
		ComputeSpriteCorners(be, xyDraw, roll, viewYaw, viewPitch, rollDegrees, c);

		SpritePiece &p = out[drawn++];
		// [rc4l] A fresh record per piece, as RegisterSprite makes one, and only for the pieces drawn.
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

// ---------------------------------------------------------------------------
// The check against GL
// ---------------------------------------------------------------------------

struct Noted { const AActor *actor; SpritePiece p; };
static TArray<Noted> g_gl, g_ours;
static int g_checkedActors = 0, g_differedActors = 0, g_onlyGL = 0, g_onlyOurs = 0;
static FString g_diffs;

void NoteGLSprite(const AActor *actor, const MeshPiece &piece, const MeshVertex *quad)
{
	if (!fua_sprite_check) return;
	Noted n;
	n.actor = actor;
	n.p.piece = piece;
	memcpy(n.p.quad, quad, sizeof(n.p.quad));
	g_gl.Push(n);
}

static bool Same(float a, float b) { return memcmp(&a, &b, sizeof(float)) == 0; }

// [rc4l] The fields the backend reads from a sprite piece, compared bit for bit; the first that differs, or NULL.
static const char *Differs(const SpritePiece &a, const SpritePiece &b)
{
	const MeshPiece &x = a.piece, &y = b.piece;
	if (x.material != y.material) return "material";
	for (int i = 0; i < 4; i++)
		if (!Same(a.quad[i].x, b.quad[i].x) || !Same(a.quad[i].y, b.quad[i].y) || !Same(a.quad[i].z, b.quad[i].z)) return "corner";
	for (int i = 0; i < 4; i++)
		if (!Same(a.quad[i].u, b.quad[i].u) || !Same(a.quad[i].v, b.quad[i].v)) return "uv";
	if (x.lightLevel != y.lightLevel || x.lightColor != y.lightColor || x.fadeColor != y.fadeColor) return "light";
	if (!Same(x.colorR, y.colorR) || !Same(x.colorG, y.colorG) || !Same(x.colorB, y.colorB) || !Same(x.softLight, y.softLight) ||
	    !Same(x.fogDensity, y.fogDensity) || x.fogColor != y.fogColor || x.fogMode != y.fogMode || x.desaturation != y.desaturation) return "shading";
	if (!Same(x.dynR, y.dynR) || !Same(x.dynG, y.dynG) || !Same(x.dynB, y.dynB)) return "dynamic light";
	if (x.translation != y.translation) return "translation";
	if (!Same(x.alpha, y.alpha) || x.blendMode != y.blendMode) return "style";
	if (!Same(x.sortX, y.sortX) || !Same(x.sortY, y.sortY) || !Same(x.sortZ, y.sortZ)) return "sort point";
	if (x.mirrorLine != y.mirrorLine || x.stackSlot != y.stackSlot) return "walk";
	return NULL;
}

void CheckFrameSprites()
{
	if (!fua_sprite_check) { g_gl.Clear(); g_ours.Clear(); return; }
	// [rc4l] Both lists are in actor order within an actor; match each actor's run of pieces against the other's.
	TMap<const AActor *, int> glFirst;
	for (unsigned i = 0; i < g_gl.Size(); i++) if (!glFirst.CheckKey(g_gl[i].actor)) glFirst[g_gl[i].actor] = (int)i;
	TMap<const AActor *, bool> seen;
	for (unsigned i = 0; i < g_ours.Size(); )
	{
		const AActor *a = g_ours[i].actor;
		unsigned j = i;
		while (j < g_ours.Size() && g_ours[j].actor == a) j++;
		seen[a] = true;
		g_checkedActors++;
		int *first = glFirst.CheckKey(a);
		if (first == NULL) { g_onlyOurs++; g_differedActors++; i = j; continue; }
		unsigned k = (unsigned)*first, n = 0;
		const char *why = NULL;
		for (; k < g_gl.Size() && g_gl[k].actor == a; k++, n++)
		{
			if (i + n >= j) { why = "piece count"; break; }
			why = Differs(g_gl[k].p, g_ours[i + n].p);
			if (why) break;
		}
		if (!why && i + n != j) why = "piece count";
		if (why)
		{
			if (g_differedActors < 12)
			{
				const SpritePiece &gp = g_gl[*first].p, &op = g_ours[i].p;
				g_diffs.AppendFormat("  %s at (%.0f, %.0f): %s; GL corner %.9g %.9g %.9g, ours %.9g %.9g %.9g\n",
					a->GetClass()->TypeName.GetChars(), FIXED2FLOAT(a->x), FIXED2FLOAT(a->y), why,
					gp.quad[0].x, gp.quad[0].z, gp.quad[0].y, op.quad[0].x, op.quad[0].z, op.quad[0].y);
			}
			g_differedActors++;
		}
		i = j;
	}
	for (unsigned i = 0; i < g_gl.Size(); i++)
		if (!seen.CheckKey(g_gl[i].actor)) { g_onlyGL++; seen[g_gl[i].actor] = true; }
	g_gl.Clear();
	g_ours.Clear();
}

bool BuildSprite(AActor *thing)
{
	// [rc4l] Kept between actors: constructing sixteen records an actor cost more than deriving it.
	static SpritePiece pieces[kMaxBands];
	const int n = DeriveSprite(thing, pieces, kMaxBands);
	if (n < 0) return false;
	if (fua_sprite_check)
	{
		for (int i = 0; i < n; i++) { Noted no; no.actor = thing; no.p = pieces[i]; g_ours.Push(no); }
		return false;
	}
	if (!zx::levelmesh::DynamicCaptureActive()) return true;
	for (int i = 0; i < n; i++) zx::levelmesh::DynAppend(pieces[i].quad, 4, pieces[i].piece);
	return true;
}

}} // namespace zx::sprites

CCMD( fua_sprite_report )
{
	Printf( "sprites: %d actors derived beside GL, %d differed (%d only ours, %d only GL's)\n%s",
		zx::sprites::g_checkedActors, zx::sprites::g_differedActors, zx::sprites::g_onlyOurs, zx::sprites::g_onlyGL,
		zx::sprites::g_diffs.GetChars( ) );
	zx::sprites::g_checkedActors = zx::sprites::g_differedActors = zx::sprites::g_onlyOurs = zx::sprites::g_onlyGL = 0;
	zx::sprites::g_diffs = "";
}
