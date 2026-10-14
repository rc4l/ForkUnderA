// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/computation/pspritequad_compute.h"

namespace zx { namespace sprites {

bool ComputePSpriteQuad(const PSpriteQuadIn &in, PSpriteQuad &out)
{
	const float vw = in.viewWidth, vh = in.viewHeight;
	// [rc4l] 4:3 over the screen's ratio, so a widescreen view does not stretch the weapon; a tall screen keeps it whole.
	const float scalex = (!in.tallerThanWide ? (4.0f / 3.0f) / in.widescreenRatio : 1.0f) * vw / 320;
	float tx = in.sx - (160 - in.rectLeft * in.scaleX);
	float x1 = tx * scalex + vw / 2;
	if (x1 > vw) return false;
	x1 += in.windowX;
	tx += in.rectWidth * in.scaleX;
	float x2 = tx * scalex + vw / 2;
	if (x2 < 0) return false;
	x2 += in.windowX;

	// [rc4l] killough 12/98's texture middle, lowered by the weapon's YAdjust.
	const float ftexturemid = 100.f - in.sy - in.rectTop * in.scaleY - in.yAdjust;
	const float scale = (in.screenHeight * vw) / (in.screenWidth * 200.0f);
	out.y1 = in.windowY + vh / 2 - (ftexturemid * scale);
	out.y2 = out.y1 + (in.rectHeight * in.scaleY * scale) + 1;
	if (in.mirror)
	{
		const float center = in.windowX + (vw / 2);
		const float mx1 = 2 * center - x2;
		x2 = 2 * center - x1;
		x1 = mx1;
	}
	out.x1 = x1;
	out.x2 = x2;
	return true;
}

int ComputeSoftWeaponLight(int calcLevel)
{
	// [rc4l] Korshun: the brightest the sector could be in software on its 0..63 scale, held in a float as GL holds it.
	float minL = (float)(36.0 / 31.0 - ((calcLevel / 255.0) * (63.0 / 31.0)));
	if (minL < 0) minL = 0;
	else if (minL > 1.0) minL = 1.0;
	return (int)((1.0 - minL) * 255);
}

int ComputeBrightenedWeaponLight(int lightLevel, bool anyBright, int lightMode)
{
	return (anyBright && lightMode != 8) ? (2 * lightLevel + 255) / 3 : lightLevel;
}

}} // namespace zx::sprites
