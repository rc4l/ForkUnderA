/*
** gl_weapon.cpp
** Weapon sprite drawing
**
**---------------------------------------------------------------------------
** Copyright 2002-2005 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
** 4. When not used as part of GZDoom or a GZDoom derivative, this code will be
**    covered by the terms of the GNU Lesser General Public License as published
**    by the Free Software Foundation; either version 2.1 of the License, or (at
**    your option) any later version.
** 5. Full disclosure of the entire project's source code, except for third
**    party libraries is mandatory. (NOTE: This clause is non-negotiable!)
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/
#include "gl/system/gl_system.h"
#include "sbar.h"
#include "r_utility.h"
#include "v_video.h"
#include "doomstat.h"
#include "d_player.h"
#include "g_level.h"

#include "gl/system/gl_interface.h"
#include "gl/system/gl_cvars.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/renderer/gl_lightdata.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/data/gl_data.h"
#include "gl/dynlights/gl_glow.h"
#include "gl/scene/gl_drawinfo.h"
#include "gl/models/gl_models.h"
#include "gl/shaders/gl_shader.h"
#include "gl/textures/gl_material.h"
#include "computation/psprite_overlay_compute.h"

EXTERN_CVAR (Bool, r_drawplayersprites)
EXTERN_CVAR(Float, transsouls)
EXTERN_CVAR (Bool, st_scale)
EXTERN_CVAR(Int, gl_fuzztype)
EXTERN_CVAR (Bool, r_deathcamera)


//==========================================================================
//
// R_DrawPSprite
//
//==========================================================================

void FGLRenderer::DrawPSprite (player_t * player,pspdef_t *psp,fixed_t sx, fixed_t sy, int cm_index, bool hudModelStep, int OverrideShader)
{
	float			fU1,fV1;
	float			fU2,fV2;
	fixed_t			tx;
	int				x1,y1,x2,y2;
	float			scale;
	fixed_t			scalex;
	fixed_t			texturemid;// 4:3		16:9		16:10			17:10			5:4
	static fixed_t xratio[] = {FRACUNIT, FRACUNIT*3/4, FRACUNIT*5/6, FRACUNIT*40/51, FRACUNIT};
	
	// [BB] In the HUD model step we just render the model and break out. 
	if ( hudModelStep )
	{
		gl_RenderHUDModel( psp, sx, sy, cm_index );
		return;
	}

	// decide which patch to use
	bool mirror;
	FTextureID lump = gl_GetSpriteFrame(psp->sprite, psp->frame, 0, 0, &mirror);
	if (!lump.isValid()) return;

	// [overlay] PSPF_FLIP mirrors the sprite's pixels (not its position).
	if (psp->Flags & PSPF_FLIP)
		mirror = !mirror;

	FMaterial * tex = FMaterial::ValidateTexture(lump, false);
	if (!tex) return;

	// [overlay] PSPF_PLAYERTRANSLATED remaps the sprite with the player's colour translation
	// (the same one used for their body sprite), for overlays representing the player.
	int psptranslation = 0;
	if ((psp->Flags & PSPF_PLAYERTRANSLATED) && player->mo != NULL)
		psptranslation = (int)player->mo->Translation;

	tex->BindPatch(cm_index, psptranslation, OverrideShader);

	int vw = viewwidth;
	int vh = viewheight;

	// calculate edges of the shape
	scalex = xratio[WidescreenRatio] * vw / 320;

	// [overlay] A_OverlayScale: the layer scale multiplies the sprite's offset and size so it
	// scales about its own offset origin (matching GZDoom). psp->scalex defaults to FRACUNIT,
	// in which case FixedMul is the identity and this is byte-identical to the original.
	// The (int) casts are required now that fixed_t (zx::Fixed) does not implicitly narrow.
	tx = sx - (160<<FRACBITS) - FixedMul(tex->GetScaledLeftOffset(GLUSE_PATCH)<<FRACBITS, psp->scalex);
	x1 = (int)((FixedMul(tx, scalex)>>FRACBITS) + (vw>>1));
	if (x1 > vw)	return; // off the right side
	x1+=viewwindowx;

	tx +=  FixedMul(tex->TextureWidth(GLUSE_PATCH)<<FRACBITS, psp->scalex);
	x2 = (int)((FixedMul(tx, scalex)>>FRACBITS) + (vw>>1));
	if (x2 < 0) return; // off the left side
	x2+=viewwindowx;

	// killough 12/98: fix psprite positioning problem
	// [overlay] The scaled top offset keeps the vertical anchor at the sprite's offset origin.
	texturemid = (100<<FRACBITS) - (sy - FixedMul(tex->GetScaledTopOffset(GLUSE_PATCH)<<FRACBITS, psp->scaley));

	AWeapon * wi=player->ReadyWeapon;
	if (wi && wi->YAdjust)
	{
		if (screenblocks>=11)
		{
			texturemid -= wi->YAdjust;
		}
		else if (!st_scale)
		{
			texturemid -= FixedMul (StatusBar->GetDisplacement (), wi->YAdjust);
		}
	}

	scale = ((SCREENHEIGHT*vw)/SCREENWIDTH) / 200.0f;
	y1 = viewwindowy + (vh >> 1) - (int)(((float)texturemid / (float)FRACUNIT) * scale);
	// [overlay] The layer's vertical scale multiplies the sprite height (identity at 1x).
	y2 = y1 + (int)((float)tex->TextureHeight(GLUSE_PATCH) * scale * FIXED2FLOAT(psp->scaley)) + 1;

	// [overlay] PSPF_MIRROR reflects the sprite's horizontal position about the view centre,
	// keeping the pixels' orientation (combine with PSPF_FLIP for a proper handedness flip).
	if (psp->Flags & PSPF_MIRROR)
	{
		int center = viewwindowx + (vw >> 1);
		int nx1 = 2 * center - x2;
		int nx2 = 2 * center - x1;
		x1 = nx1;
		x2 = nx2;
	}

	if (!mirror)
	{
		fU1=tex->GetUL();
		fV1=tex->GetVT();
		fU2=tex->GetUR();
		fV2=tex->GetVB();
	}
	else
	{
		fU2=tex->GetUL();
		fV1=tex->GetVT();
		fU1=tex->GetUR();
		fV2=tex->GetVB();
	}

	if (tex->GetTransparent() || OverrideShader != 0)
	{
		gl_RenderState.EnableAlphaTest(false);
	}
	gl_RenderState.Apply();
	glBegin(GL_TRIANGLE_STRIP);
	glTexCoord2f(fU1, fV1); glVertex2f(x1,y1);
	glTexCoord2f(fU1, fV2); glVertex2f(x1,y2);
	glTexCoord2f(fU2, fV1); glVertex2f(x2,y1);
	glTexCoord2f(fU2, fV2); glVertex2f(x2,y2);
	glEnd();
	if (tex->GetTransparent() || OverrideShader != 0)
	{
		gl_RenderState.EnableAlphaTest(true);
	}
}

//==========================================================================
//
// R_DrawPlayerSprites
//
//==========================================================================

EXTERN_CVAR(Bool, gl_brightfog)

void FGLRenderer::DrawPlayerSprites(sector_t * viewsector, bool hudModelStep)
{
	unsigned int i;
	pspdef_t *psp;
	// [overlay] Per-layer fullbright, plus the two global overrides (fixed colormap / fuzz).
	bool allbright = false;
	bool nobright = false;
	TArray<pspdef_t *> drawlist;	// weapon-pass layers (below the targeter), ascending draw order
	TArray<bool> statebrightlist;	// parallel to drawlist
	int lightlevel=0;
	fixed_t ofsx, ofsy;
	FColormap cm;
	sector_t * fakesec, fs;
	AActor * playermo=players[consoleplayer].camera;
	player_t * player=playermo->player;
	
	// this is the same as the software renderer
	if (!player ||
		!r_drawplayersprites ||
		!camera->player ||
		// [EP] similarly to the change in the software renderer, weapon display
		// for the chasecam case must be checked only for the console player.
		(/*player->*/players[consoleplayer].cheats & CF_CHASECAM) || 
		(r_deathcamera && camera->health <= 0))
		return;

	/*
	if(!player || playermo->renderflags&RF_INVISIBLE || !r_drawplayersprites ||
		mViewActor!=playermo || playermo->RenderStyle.BlendOp == STYLEOP_None) return;
	*/

	P_BobWeapon (player, &player->psprites[ps_weapon], &ofsx, &ofsy);

	// [overlay] Collect the weapon-pass layers (everything below the targeter) in ascending
	// draw order, and compute each layer's fullbright exactly as the old statebright[] did.
	for (i = 0; i < player->psprites.Size(); i++)
	{
		pspdef_t &pl = player->psprites.Element(i);
		if (pl.layer >= ps_targetcenter || pl.state == NULL)
			continue;

		drawlist.Push(&pl);

		bool bright = false;
		if (player->fixedcolormap==NOFIXEDCOLORMAP)
		{
			bool disablefullbright = false;
			FTextureID lump = gl_GetSpriteFrame(pl.sprite, pl.frame, 0, 0, NULL);
			if (lump.isValid() && gl_BrightmapsActive())
			{
				FMaterial * tex=FMaterial::ValidateTexture(lump, false);
				if (tex)
					disablefullbright = tex->tex->gl_info.bBrightmapDisablesFullbright;
			}
			bright = !!pl.state->GetFullbright() && !disablefullbright;
		}
		statebrightlist.Push(bright);
	}

	if (gl_fixedcolormap) 
	{
		lightlevel=255;
		cm.GetFixedColormap();
		allbright = true;	// [overlay] fixed colormap makes every layer fullbright
		fakesec = viewsector;
	}
	else
	{
		fakesec    = gl_FakeFlat(viewsector, &fs, false);

		// calculate light level for weapon sprites
		lightlevel = gl_ClampLight(fakesec->lightlevel);
		if (glset.lightmode == 8)
		{
			lightlevel = gl_CalcLightLevel(lightlevel, getExtraLight(), true);

			// Korshun: the way based on max possible light level for sector like in software renderer.
			float min_L = 36.0/31.0 - ((lightlevel/255.0) * (63.0/31.0)); // Lightlevel in range 0-63
			if (min_L < 0)
				min_L = 0;
			else if (min_L > 1.0)
				min_L = 1.0;

			lightlevel = (1.0 - min_L) * 255;
		}
		lightlevel = gl_CheckSpriteGlow(viewsector, lightlevel, playermo->x, playermo->y, playermo->z);

		// calculate colormap for weapon sprites
		if (viewsector->e->XFloor.ffloors.Size() && !glset.nocoloredspritelighting)
		{
			TArray<lightlist_t> & lightlist = viewsector->e->XFloor.lightlist;
			for(i=0;i<lightlist.Size();i++)
			{
				int lightbottom;

				if (i<lightlist.Size()-1) 
				{
					lightbottom=(int)(lightlist[i+1].plane.ZatPoint(viewx,viewy));
				}
				else 
				{
					lightbottom=(int)(viewsector->floorplane.ZatPoint(viewx,viewy));
				}

				if (lightbottom<player->viewz) 
				{
					cm = lightlist[i].extra_colormap;
					lightlevel = *lightlist[i].p_lightlevel;
					break;
				}
			}
		}
		else 
		{
			cm=fakesec->ColorMap;
			if (glset.nocoloredspritelighting) cm.ClearColor();
		}
	}

	
	// Korshun: fullbright fog in opengl, render weapon sprites fullbright (but don't cancel out the light color!)
	if (glset.brightfog && ((level.flags&LEVEL_HASFADETABLE) || cm.FadeColor != 0))
	{
		lightlevel = 255;
	}

	PalEntry ThingColor = playermo->fillcolor;
	visstyle_t vis;

	vis.RenderStyle=playermo->RenderStyle;
	vis.alpha=playermo->alpha;
	vis.colormap = NULL;
	if (playermo->Inventory) 
	{
		playermo->Inventory->AlterWeaponSprite(&vis);
		if (vis.colormap >= SpecialColormaps[0].Colormap && 
			vis.colormap < SpecialColormaps[SpecialColormaps.Size()].Colormap && 
			cm.colormap == CM_DEFAULT)
		{
			ptrdiff_t specialmap = (vis.colormap - SpecialColormaps[0].Colormap) / sizeof(FSpecialColormap);
			cm.colormap = int(CM_FIRSTSPECIALCOLORMAP + specialmap);
		}
	}

	// Set the render parameters

	int OverrideShader = 0;
	float trans = 0.f;
	if (vis.RenderStyle.BlendOp >= STYLEOP_Fuzz && vis.RenderStyle.BlendOp <= STYLEOP_FuzzOrRevSub)
	{
		vis.RenderStyle.CheckFuzz();
		if (vis.RenderStyle.BlendOp == STYLEOP_Fuzz)
		{
			if (gl.shadermodel >= 4 && gl_fuzztype != 0)
			{
				// Todo: implement shader selection here
				vis.RenderStyle = LegacyRenderStyles[STYLE_Translucent];
				OverrideShader = gl_fuzztype + 4;
				trans = 0.99f;	// trans may not be 1 here
			}
			else
			{
				vis.RenderStyle.BlendOp = STYLEOP_Shadow;
			}
		}
		nobright = true;	// [overlay] fuzz forces every layer non-bright
	}

	gl_SetRenderStyle(vis.RenderStyle, false, false);

	if (vis.RenderStyle.Flags & STYLEF_TransSoulsAlpha)
	{
		trans = transsouls;
	}
	else if (vis.RenderStyle.Flags & STYLEF_Alpha1)
	{
		trans = 1.f;
	}
	else if (trans == 0.f)
	{
		trans = FIXED2FLOAT(vis.alpha);
	}

	// [overlay] Is any drawn layer fullbright? (drives the weapon-brighten below)
	bool anybright = false;
	for (i = 0; i < drawlist.Size(); i++)
	{
		if (!nobright && (allbright || statebrightlist[i]))
		{
			anybright = true;
			break;
		}
	}

	// now draw the different layers of the weapon
	gl_RenderState.EnableBrightmap(true);
	if (anybright)
	{
		// brighten the weapon to reduce the difference between
		// normal sprite and fullbright flash.
		if (glset.lightmode != 8) lightlevel = (2*lightlevel+255)/3;
	}
	
	// hack alert! Rather than changing everything in the underlying lighting code let's just temporarily change
	// light mode here to draw the weapon sprite.
	int oldlightmode = glset.lightmode;
	if (glset.lightmode == 8) glset.lightmode = 2;
	
	// [overlay] Draw every weapon-pass layer in ascending id order (further back -> in front).
	fixed_t weaponx = player->psprites[ps_weapon].sx;
	fixed_t weapony = player->psprites[ps_weapon].sy;
	for (i=0; i<drawlist.Size(); i++)
	{
		psp = drawlist[i];
		bool bright = !nobright && (allbright || statebrightlist[i]);

		FColormap cmc = cm;
		if (bright)
		{
			if (fakesec == viewsector || in_area != area_below)
				// under water areas keep most of their color for fullbright objects
			{
				cmc.LightColor.r=
				cmc.LightColor.g=
				cmc.LightColor.b=0xff;
			}
			else
			{
				cmc.LightColor.r = (3*cmc.LightColor.r + 0xff)/4;
				cmc.LightColor.g = (3*cmc.LightColor.g + 0xff)/4;
				cmc.LightColor.b = (3*cmc.LightColor.b + 0xff)/4;
			}
		}
		// [overlay] Effective render style and alpha for this layer. By default a layer uses the
		// weapon's style/alpha; overlays override via PSPF_RENDERSTYLE / PSPF_ALPHA (and their
		// FORCE variants), so the reserved weapon/flash layers stay pixel-identical.
		FRenderStyle layerStyle = vis.RenderStyle;
		int layerShader = OverrideShader;
		if ((psp->Flags & (PSPF_RENDERSTYLE | PSPF_FORCESTYLE)) && psp->RenderStyle.AsDWORD != 0)
		{
			layerStyle = psp->RenderStyle;
			layerShader = 0;
		}

		float layertrans = trans;
		if (psp->Flags & (PSPF_ALPHA | PSPF_FORCEALPHA))
			layertrans = FIXED2FLOAT(psp->alpha);

		gl_SetRenderStyle(layerStyle, false, false);

		// set the lighting parameters (only calls glColor and glAlphaFunc)
		gl_SetSpriteLighting(layerStyle, playermo, bright ? 255 : lightlevel,
			0, &cmc, 0xffffff, layertrans, bright, true);

		// [overlay] The reserved weapon/flash layers always ride the bob; overlays follow the
		// weapon offset and/or the bob only when they set the matching flag.
		bool ridesBob = (psp->layer == ps_weapon || psp->layer == ps_flash);
		// [overlay] The pure helper works on raw 48.16 fixed values (fixed_t is now zx::Fixed).
		int64_t addxRaw, addyRaw;
		ComputeOverlayDrawOffset(ridesBob, (int)psp->Flags, weaponx.Raw(), weapony.Raw(), ofsx.Raw(), ofsy.Raw(), &addxRaw, &addyRaw);
		fixed_t addx = fixed_t::FromRaw(addxRaw);
		fixed_t addy = fixed_t::FromRaw(addyRaw);

		// [overlay] PSPF_INTERPOLATE / WOF_INTERPOLATE smooth an overlay's offset between tics.
		// Reserved weapon/flash layers are left alone so stock rendering is unchanged.
		fixed_t drawsx = psp->sx;
		fixed_t drawsy = psp->sy;
		if (psp->bInterpolate && !ridesBob)
		{
			drawsx = psp->oldx + FixedMul(psp->sx - psp->oldx, r_TicFrac);
			drawsy = psp->oldy + FixedMul(psp->sy - psp->oldy, r_TicFrac);
		}
		DrawPSprite (player,psp,drawsx+addx, drawsy+addy, cm.colormap, hudModelStep, layerShader);
	}
	gl_RenderState.EnableBrightmap(false);
	glset.lightmode = oldlightmode;
}

//==========================================================================
//
// R_DrawPlayerSprites
//
//==========================================================================

void FGLRenderer::DrawTargeterSprites()
{
	int i;
	pspdef_t *psp;
	AActor * playermo=players[consoleplayer].camera;
	player_t * player=playermo->player;
	
	if(!player || playermo->renderflags&RF_INVISIBLE || !r_drawplayersprites ||
		mViewActor!=playermo) return;

	gl_RenderState.EnableBrightmap(false);
	gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl_RenderState.AlphaFunc(GL_GEQUAL,gl_mask_sprite_threshold);
	gl_RenderState.BlendEquation(GL_FUNC_ADD);
	glColor3f(1.0f,1.0f,1.0f);
	gl_RenderState.SetTextureMode(TM_MODULATE);

	// The Targeter's sprites are always drawn normally.
	// [overlay] Iterate the three reserved targeter layers by id.
	static const int targetlayers[3] = { ps_targetcenter, ps_targetleft, ps_targetright };
	for (i=0; i<3; i++)
	{
		psp = player->psprites.Find(targetlayers[i]);
		if (psp && psp->state) DrawPSprite (player,psp,psp->sx, psp->sy, CM_DEFAULT, false, 0);
	}
}