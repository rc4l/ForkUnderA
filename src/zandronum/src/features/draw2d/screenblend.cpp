// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/draw2d/screenblend.h"
#include "features/draw2d/computation/screenblend_compute.h"
#include "features/draw2d/computation/quad2d_compute.h"   // [rc4l] ComputeScreenFillQuad
#include "features/levelmesh/levelmesh.h"   // [rc4l] LightListBottom

#include "d_player.h"
#include "doomstat.h"
#include "p_3dfloors.h"
#include "r_defs.h"
#include "r_utility.h"
#include "r_data/colormaps.h"
#include "v_palette.h"
#include "tarray.h"
#include "v_video.h"   // [rc4l] SCREENWIDTH, SCREENHEIGHT

#include <string.h>

namespace zx { namespace draw2d {

// [rc4l] The blend a sector paints over the view: its height transfer's colormap for the area the eye is in, or the 3D floor fog the eye is under.
static PalEntry SectorBlend(sector_t *viewsector, int viewArea)
{
	if (!viewsector->e->XFloor.ffloors.Size())
	{
		if (!viewsector->heightsec || (viewsector->MoreFlags & SECF_IGNOREHEIGHTSEC)) return 0;
		return PalEntry(viewArea == kViewAreaAbove ? viewsector->heightsec->topmap
			: viewArea == kViewAreaBelow ? viewsector->heightsec->bottommap : viewsector->heightsec->midmap);
	}
	const TArray<lightlist_t> &lightlist = viewsector->e->XFloor.lightlist;
	static TArray<BlendBand> bands;
	bands.Resize(lightlist.Size());
	for (unsigned i = 0; i < lightlist.Size(); i++)
	{
		bands[i].bottom = zx::levelmesh::LightListBottom(viewsector, i, viewx, viewy).Raw();
		bands[i].blend = lightlist[i].blend.d;
		bands[i].fadeWalls = lightlist[i].caster != NULL && (lightlist[i].caster->flags & FF_FADEWALLS);
	}
	return PalEntry(ComputeBandBlend(bands.Size() ? &bands[0] : NULL, (int)bands.Size(), viewz.Raw(), viewsector->ColorMap->Fade.d));
}

int ScreenBlends(sector_t *viewsector, int viewArea, bool fixedColormap, int paletteBrightness, ScreenFill out[2])
{
	float blend[4] = { 0, 0, 0, 0 };
	int n = 0;
	AActor *camera = players[consoleplayer].camera;
	// [rc4l] Invulnerability's colormap leaves out the sector's blend.
	if (!fixedColormap)
	{
		PalEntry blendv = SectorBlend(viewsector, viewArea);
		if (blendv.a == 0) blendv = PalEntry(ComputeBrightenedBlend(R_BlendForColormap(blendv), paletteBrightness));
		if (blendv.a == 255)
		{
			// [rc4l] A colormap multiplies the screen, unless it is black.
			const float r = blendv.r / 255.0f, g = blendv.g / 255.0f, b = blendv.b / 255.0f;
			if (r || g || b) { const ScreenFill f = { r, g, b, 1.0f, true }; out[n++] = f; }
		}
		else if (blendv.a) V_AddBlend(blendv.r / 255.f, blendv.g / 255.f, blendv.b / 255.f, blendv.a / 255.0f, blend);
	}
	if (camera != NULL && camera->player != NULL) V_AddPlayerBlend(camera->player, blend, 0.5, 175);
	if (camera != NULL)
	{
		// [rc4l] The fade a script sets lives on the camera's player, or the console player's when the camera is not one.
		player_t *player = camera->player != NULL ? camera->player : &players[consoleplayer];
		V_AddBlend(player->BlendR, player->BlendG, player->BlendB, player->BlendA, blend);
	}
	if (blend[3] > 0.0f) { const ScreenFill f = { blend[0], blend[1], blend[2], blend[3], false }; out[n++] = f; }
	return n;
}

zx::draw2d::Quad2D PrepareScreenFill(const ScreenFill &f)
{
	return ComputeScreenFillQuad((float)SCREENWIDTH, (float)SCREENHEIGHT, f.r, f.g, f.b, f.a, f.multiply);
}

}} // namespace zx::draw2d
