// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/sprites/pspritebuild.h"

#include "features/sprites/spritebuild.h"     // [rc4l] ActorDynamicLight
#include "features/sprites/computation/pspritequad_compute.h"
#include "features/sprites/computation/spritelight_compute.h"
#include "features/draw2d/computation/quad2d_compute.h"
#include "features/levelmesh/computation/shading_compute.h"
#include "features/hwrender/hud2d.h"
#include "features/hwrender/spriteview.h"
#include "features/materials/materials.h"
#include "computation/psprite_overlay_compute.h"

#include "sbar.h"
#include "r_state.h"
#include "r_utility.h"
#include "v_video.h"
#include "doomstat.h"
#include "d_player.h"
#include "g_level.h"
#include "p_3dfloors.h"
#include "c_dispatch.h"
#include "r_data/colormaps.h"
#include "r_data/renderstyle.h"
#include "textures/textures.h"
#include "g_shared/a_pickups.h"

// [rc4l] GL's view state and settings the derivation still reads, until the renderer interface supplies them (P5).
#include "gl/renderer/gl_renderer.h"    // gl_FakeFlat, area_below
#include "gl/renderer/gl_lightdata.h"   // gl_ShadingEnv, gl_ClampLight, getExtraLight
#include "gl/renderer/gl_colormap.h"
#include "gl/data/gl_data.h"            // glset, gl_GetSpriteFrame
#include "gl/system/gl_cvars.h"         // screenblocks

#include <string.h>
#include <vector>

EXTERN_CVAR(Bool, r_drawplayersprites)
EXTERN_CVAR(Bool, r_deathcamera)
EXTERN_CVAR(Bool, st_scale)
EXTERN_CVAR(Float, transsouls)
EXTERN_CVAR(Int, gl_fuzztype)
EXTERN_CVAR(Bool, gl_lights)
EXTERN_CVAR(Bool, gl_light_sprites)

// [rc4l] The backend's weapon derived here rather than recorded from GL's DrawPSprite.
CVAR(Bool, fua_psprites_own, true, 0)
// [rc4l] Derive the weapon beside GL's and compare the two quad for quad, for fua_psprite_report.
CVAR(Bool, fua_psprite_check, false, 0)

namespace zx { namespace sprites {

namespace {

std::vector<zx::hwrender::Quad2D> g_gl, g_ours;
int g_checkedFrames = 0, g_differedFrames = 0, g_checkedQuads = 0;
FString g_diffs;

// [rc4l] A derived quad, recorded for the backend or, with the check on, held for the comparison.
void Emit(const zx::hwrender::Quad2D &q)
{
	if (fua_psprite_check) g_ours.push_back(q);
	else zx::hwrender::Record2D(q);
}

// [rc4l] DrawPSprite for one layer at (sx, sy): its frame, its screen rectangle, and the colour the caller worked out, with the dynamic light folded in as GL's record folds it.
void LayerQuad(player_t *player, pspdef_t *psp, fixed_t sx, fixed_t sy, int translation, const float rgba[4], const float dyn[3],
	int blend, int texMode)
{
	bool mirror;
	FTextureID lump = gl_GetSpriteFrame(psp->sprite, psp->frame, 0, 0, &mirror);
	if (!lump.isValid()) return;
	if (psp->Flags & PSPF_FLIP) mirror = !mirror;
	const void *material = zx::materials::MaterialForTexture(TexMan[lump], true);
	zx::materials::SpriteFrameInfo fi;
	if (!zx::materials::MaterialSpriteFrame(material, fi)) return;

	PSpriteQuadIn in;
	in.sx = FIXED2FLOAT(sx); in.sy = FIXED2FLOAT(sy);
	in.rectLeft = fi.left; in.rectTop = fi.top; in.rectWidth = fi.width; in.rectHeight = fi.height;
	in.scaleX = FIXED2FLOAT(psp->scalex); in.scaleY = FIXED2FLOAT(psp->scaley);
	in.viewWidth = (float)viewwidth; in.viewHeight = (float)viewheight;
	in.windowX = (float)viewwindowx; in.windowY = (float)viewwindowy;
	in.widescreenRatio = WidescreenRatio;
	in.tallerThanWide = AspectTallerThanWide(WidescreenRatio);
	in.yAdjust = 0.f;
	AWeapon *wi = player->ReadyWeapon;
	if (wi && wi->YAdjust)
	{
		const float fYAd = FIXED2FLOAT(wi->YAdjust);
		if (screenblocks >= 11) in.yAdjust = fYAd;
		else if (!st_scale) in.yAdjust = FIXED2FLOAT(StatusBar->GetDisplacement()) * fYAd;
	}
	in.screenWidth = (float)SCREENWIDTH; in.screenHeight = (float)SCREENHEIGHT;
	in.mirror = (psp->Flags & PSPF_MIRROR) != 0;
	PSpriteQuad r;
	if (!ComputePSpriteQuad(in, r)) return;

	zx::hwrender::Quad2D q;
	memset(&q, 0, sizeof(q));
	q.material = material;
	q.x = r.x1; q.y = r.y1; q.w = r.x2 - r.x1; q.h = r.y2 - r.y1;
	// [rc4l] A mirrored frame swaps its sprite UVs, as DrawPSprite takes them.
	q.u1 = mirror ? fi.ur : fi.ul; q.u2 = mirror ? fi.ul : fi.ur;
	q.v1 = fi.vt; q.v2 = fi.vb;
	q.r = rgba[0] + dyn[0] > 1.f ? 1.f : rgba[0] + dyn[0];
	q.g = rgba[1] + dyn[1] > 1.f ? 1.f : rgba[1] + dyn[1];
	q.b = rgba[2] + dyn[2] > 1.f ? 1.f : rgba[2] + dyn[2];
	q.a = rgba[3];
	q.blend = blend;
	q.translation = translation;
	q.texMode = texMode;
	Emit(q);
}

// [rc4l] Every field the backend draws from, bit for bit; the window a quad is for is stamped later and left out.
bool SameQuad(const zx::hwrender::Quad2D &a, const zx::hwrender::Quad2D &b)
{
	const float fa[] = { a.x, a.y, a.w, a.h, a.u1, a.v1, a.u2, a.v2, a.r, a.g, a.b, a.a, a.lx2, a.ly2 };
	const float fb[] = { b.x, b.y, b.w, b.h, b.u1, b.v1, b.u2, b.v2, b.r, b.g, b.b, b.a, b.lx2, b.ly2 };
	return a.material == b.material && memcmp(fa, fb, sizeof(fa)) == 0 && a.clipL == b.clipL && a.clipT == b.clipT && a.clipR == b.clipR &&
		a.clipB == b.clipB && a.blend == b.blend && a.translation == b.translation && a.texMode == b.texMode && a.kind == b.kind;
}

// [rc4l] PSPF_PLAYERTRANSLATED takes the player's own colours.
int LayerTranslation(const player_t *player, const pspdef_t *psp)
{
	return ((psp->Flags & PSPF_PLAYERTRANSLATED) && player->mo != NULL) ? (int)player->mo->Translation : 0;
}

} // namespace

bool PSpritesOwned()
{
	return fua_psprites_own && !fua_psprite_check;
}

void BuildPlayerSprites(sector_t *viewsector, int viewArea, fixed_t bobX, fixed_t bobY)
{
	if (!fua_psprites_own && !fua_psprite_check) return;
	AActor *playermo = players[consoleplayer].camera;
	player_t *player = playermo->player;
	// [rc4l] As the software renderer: no weapon from a chase camera or a dead camera's eye.
	if (!player || !r_drawplayersprites || !camera->player || (players[consoleplayer].cheats & CF_CHASECAM) ||
	    (r_deathcamera && camera->health <= 0)) return;

	// [rc4l] [overlay] The layers below the targeter in draw order, each fullbright unless its frame's GLDEFS says otherwise.
	std::vector<pspdef_t *> layers;
	std::vector<bool> stateBright;
	for (unsigned i = 0; i < player->psprites.Size(); i++)
	{
		pspdef_t &pl = player->psprites.Element(i);
		if (pl.layer >= ps_targetcenter || pl.state == NULL) continue;
		layers.push_back(&pl);
		bool bright = false;
		if (player->fixedcolormap == NOFIXEDCOLORMAP)
		{
			const FTextureID lump = gl_GetSpriteFrame(pl.sprite, pl.frame, 0, 0, NULL);
			zx::materials::SpriteFrameInfo fi;
			const bool disable = lump.isValid() && zx::materials::MaterialSpriteFrame(zx::materials::MaterialForTexture(TexMan[lump], false), fi) &&
				fi.disableFullbright;
			bright = !!pl.state->GetFullbright() && !disable;
		}
		stateBright.push_back(bright);
	}

	zx::levelmesh::ShadingEnv env = gl_ShadingEnv();
	int lightlevel = 0;
	FColormap cm;
	sector_t fs;
	sector_t *fakesec = viewsector;
	bool allbright = false, nobright = false;
	if (gl_fixedcolormap)
	{
		lightlevel = 255;
		cm.Clear();
		allbright = true;
	}
	else
	{
		fakesec = gl_FakeFlat(viewsector, &fs, false);
		lightlevel = gl_ClampLight(fakesec->lightlevel);
		if (glset.lightmode == 8) lightlevel = ComputeSoftWeaponLight(zx::levelmesh::ComputeLightLevel(lightlevel, getExtraLight(), true, env));
		FTexture *floortex = TexMan[viewsector->GetTexture(sector_t::floor)];
		if (floortex != NULL && floortex->isGlowing())
			lightlevel = ComputeSpriteGlow(lightlevel, (int)(playermo->z - viewsector->floorplane.ZatPoint(playermo->x, playermo->y)).Raw(),
				floortex->gl_info.GlowHeight);
		if (viewsector->e->XFloor.ffloors.Size() && !glset.nocoloredspritelighting)
		{
			// [rc4l] The 3D floor band the eye is in lights and colours the weapon.
			TArray<lightlist_t> &lightlist = viewsector->e->XFloor.lightlist;
			for (unsigned i = 0; i < lightlist.Size(); i++)
			{
				const fixed_t lightbottom = i < lightlist.Size() - 1 ? lightlist[i + 1].plane.ZatPoint(viewx, viewy)
					: viewsector->floorplane.ZatPoint(viewx, viewy);
				if (lightbottom < player->viewz)
				{
					cm = lightlist[i].extra_colormap;
					lightlevel = *lightlist[i].p_lightlevel;
					break;
				}
			}
		}
		else
		{
			cm = fakesec->ColorMap;
			if (glset.nocoloredspritelighting) cm.ClearColor();
		}
	}
	// [rc4l] Korshun: bright fog draws the weapon fullbright, keeping its light colour.
	if (glset.brightfog && ((level.flags & LEVEL_HASFADETABLE) || cm.FadeColor != 0)) lightlevel = 255;

	visstyle_t vis;
	memset(&vis, 0, sizeof(vis));
	vis.RenderStyle = playermo->RenderStyle;
	vis.alpha = playermo->alpha;
	vis.colormap = NULL;
	if (playermo->Inventory)
	{
		playermo->Inventory->AlterWeaponSprite(&vis);
		// [rc4l] Strife's inverted weapon sprite.
		if (vis.colormap >= SpecialColormaps[0].Colormap && vis.colormap < SpecialColormaps[SpecialColormaps.Size()].Colormap &&
		    gl_fixedcolormap == CM_DEFAULT) vis.RenderStyle.Flags |= STYLEF_InvertSource;
	}
	float trans = 0.f;
	if (vis.RenderStyle.BlendOp >= STYLEOP_Fuzz && vis.RenderStyle.BlendOp <= STYLEOP_FuzzOrRevSub)
	{
		vis.RenderStyle.CheckFuzz();
		if (vis.RenderStyle.BlendOp == STYLEOP_Fuzz)
		{
			if (gl_fuzztype != 0)
			{
				vis.RenderStyle = LegacyRenderStyles[STYLE_Translucent];
				trans = 0.99f;
			}
			else vis.RenderStyle.BlendOp = STYLEOP_Shadow;
		}
		nobright = true;
	}
	if (vis.RenderStyle.Flags & STYLEF_TransSoulsAlpha) trans = transsouls;
	else if (vis.RenderStyle.Flags & STYLEF_Alpha1) trans = 1.f;
	else if (trans == 0.f) trans = FIXED2FLOAT(vis.alpha);

	bool anybright = false;
	for (size_t i = 0; i < layers.size() && !anybright; i++) anybright = !nobright && (allbright || stateBright[i]);
	lightlevel = ComputeBrightenedWeaponLight(lightlevel, anybright, glset.lightmode);
	// [rc4l] GL draws the weapon under light mode 2 when the level is in software lighting's 8.
	if (env.lightMode == 8) env.lightMode = 2;

	// [rc4l] GL's dynamic light persists from layer to layer, set only by a lit one, and the world's draws leave it at zero.
	float dyn[3] = { 0.f, 0.f, 0.f };
	const fixed_t weaponx = player->psprites[ps_weapon].sx, weapony = player->psprites[ps_weapon].sy;
	for (size_t i = 0; i < layers.size(); i++)
	{
		pspdef_t *psp = layers[i];
		const bool bright = !nobright && (allbright || stateBright[i]);
		FColormap cmc = cm;
		if (bright)
		{
			// [rc4l] Under water a fullbright layer keeps most of the colour.
			if (fakesec == viewsector || viewArea != area_below) cmc.LightColor.r = cmc.LightColor.g = cmc.LightColor.b = 0xff;
			else
			{
				cmc.LightColor.r = (3 * cmc.LightColor.r + 0xff) / 4;
				cmc.LightColor.g = (3 * cmc.LightColor.g + 0xff) / 4;
				cmc.LightColor.b = (3 * cmc.LightColor.b + 0xff) / 4;
			}
		}
		FRenderStyle layerStyle = vis.RenderStyle;
		if ((psp->Flags & (PSPF_RENDERSTYLE | PSPF_FORCESTYLE)) && psp->RenderStyle.AsDWORD != 0) layerStyle = psp->RenderStyle;
		float layertrans = trans;
		if (psp->Flags & (PSPF_ALPHA | PSPF_FORCEALPHA)) layertrans = FIXED2FLOAT(psp->alpha);

		float rgba[4];
		if (layerStyle.BlendOp == STYLEOP_Shadow)
		{
			rgba[0] = rgba[1] = rgba[2] = 0.2f;
			rgba[3] = 0.33f;
		}
		else
		{
			if (gl_lights && zx::hwrender::SpriteView().DynamicLightCount() && !gl_fixedcolormap && gl_light_sprites) ActorDynamicLight(playermo, dyn);
			// [rc4l] gl_SetColor for a weapon: white under a fixed colormap, else the light colour at the weapon's light level.
			if (gl_fixedcolormap != CM_DEFAULT) rgba[0] = rgba[1] = rgba[2] = 1.f;
			else
			{
				int r, g, b;
				const int hw = zx::levelmesh::ComputeLightLevel(bright ? 255 : lightlevel, 0, true, env);
				zx::levelmesh::ComputeLightColor(hw, cmc.LightColor.d, cmc.blendfactor, env.lightMode, r, g, b);
				rgba[0] = r / 255.f; rgba[1] = g / 255.f; rgba[2] = b / 255.f;
			}
			rgba[3] = layertrans;
		}

		// [rc4l] [overlay] The weapon and flash ride the bob; an overlay follows the weapon's offset and the bob as its flags say, interpolating when asked.
		const bool ridesBob = (psp->layer == ps_weapon || psp->layer == ps_flash);
		int64_t addxRaw, addyRaw;
		ComputeOverlayDrawOffset(ridesBob, (int)psp->Flags, weaponx.Raw(), weapony.Raw(), bobX.Raw(), bobY.Raw(), &addxRaw, &addyRaw);
		fixed_t drawsx = psp->sx, drawsy = psp->sy;
		if (psp->bInterpolate && !ridesBob)
		{
			drawsx = psp->oldx + FixedMul(psp->sx - psp->oldx, r_TicFrac);
			drawsy = psp->oldy + FixedMul(psp->sy - psp->oldy, r_TicFrac);
		}
		LayerQuad(player, psp, drawsx + fixed_t::FromRaw(addxRaw), drawsy + fixed_t::FromRaw(addyRaw), LayerTranslation(player, psp), rgba, dyn,
			zx::draw2d::ComputeQuadBlend(layerStyle.BlendOp, layerStyle.DestAlpha), zx::draw2d::ComputeTextureMode(layerStyle.Flags, false));
	}
}

void BuildTargeterSprites(AActor *viewActor)
{
	if (!fua_psprites_own && !fua_psprite_check) return;
	AActor *playermo = players[consoleplayer].camera;
	player_t *player = playermo->player;
	if (!player || (playermo->renderflags & RF_INVISIBLE) || !r_drawplayersprites || viewActor != playermo) return;
	// [rc4l] The targeter is always drawn plain: white, unlit, alpha blended.
	static const float white[4] = { 1.f, 1.f, 1.f, 1.f };
	static const float none[3] = { 0.f, 0.f, 0.f };
	static const int targetLayers[3] = { ps_targetcenter, ps_targetleft, ps_targetright };
	for (int i = 0; i < 3; i++)
	{
		pspdef_t *psp = player->psprites.Find(targetLayers[i]);
		if (psp && psp->state)
			LayerQuad(player, psp, psp->sx, psp->sy, LayerTranslation(player, psp), white, none, zx::draw2d::kBlend2DAlpha, zx::draw2d::kTexModulate);
	}
}

void NoteGLPSprite(const zx::hwrender::Quad2D &q)
{
	if (fua_psprite_check) g_gl.push_back(q);
}

void CheckFramePSprites()
{
	if (!fua_psprite_check) { g_gl.clear(); g_ours.clear(); return; }
	g_checkedFrames++;
	g_checkedQuads += (int)g_gl.size();
	const char *why = g_gl.size() != g_ours.size() ? "quad count" : NULL;
	for (size_t i = 0; why == NULL && i < g_gl.size(); i++)
		if (!SameQuad(g_gl[i], g_ours[i])) why = "quad";
	if (why != NULL)
	{
		if (g_differedFrames < 8)
		{
			const zx::hwrender::Quad2D none = {};
			const zx::hwrender::Quad2D &a = g_gl.empty() ? none : g_gl[0], &b = g_ours.empty() ? none : g_ours[0];
			g_diffs.AppendFormat("  %s: GL %u quads at %.6g,%.6g %.6gx%.6g rgba %.6g %.6g %.6g %.6g blend %d tm %d tr %d; ours %u at %.6g,%.6g %.6gx%.6g rgba %.6g %.6g %.6g %.6g blend %d tm %d tr %d\n",
				why, (unsigned)g_gl.size(), a.x, a.y, a.w, a.h, a.r, a.g, a.b, a.a, a.blend, a.texMode, a.translation,
				(unsigned)g_ours.size(), b.x, b.y, b.w, b.h, b.r, b.g, b.b, b.a, b.blend, b.texMode, b.translation);
		}
		g_differedFrames++;
	}
	// [rc4l] GL's quads were recorded and drawn; ours were only compared.
	g_gl.clear();
	g_ours.clear();
}

}} // namespace zx::sprites

CCMD( fua_psprite_report )
{
	Printf( "weapon: %d frames derived beside GL (%d quads), %d differed\n%s", zx::sprites::g_checkedFrames, zx::sprites::g_checkedQuads,
		zx::sprites::g_differedFrames, zx::sprites::g_diffs.GetChars( ) );
	zx::sprites::g_checkedFrames = zx::sprites::g_differedFrames = zx::sprites::g_checkedQuads = 0;
	zx::sprites::g_diffs = "";
}
