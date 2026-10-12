// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/surfaces/computation/skywall_compute.h"

namespace zx { namespace surfaces {

static const float kLimit = 32768.0f;

static void Span(SkyWallQuad &q, bool ceiling, float b0, float b1, float t0, float t1, bool reflect)
{
	q.ceiling = ceiling;
	q.zbottom[0] = b0; q.zbottom[1] = b1;
	q.ztop[0] = t0; q.ztop[1] = t1;
	q.skyhack = false;
	q.allowReflect = reflect;
}

void ComputeSkyNormal(const float zfloor[2], const float zceil[2], SkyWallQuad out[2])
{
	Span(out[0], true, zceil[0], zceil[1], kLimit, kLimit, true);
	Span(out[1], false, -kLimit, -kLimit, zfloor[0], zfloor[1], true);
}

bool ComputeSkyTop(const SkyTopIn &in, SkyWallQuad &out)
{
	if (in.frontSky)
	{
		if (in.backNoSkyDraw) return false;
		if (in.backSky)
		{
			// [rc4l] Sky on both sides continues through an open back sector, so only a shut one gets a wall.
			if (in.backOpen) return false;
			// [rc4l] Icarus MAP14's transparent-door hack, only for a floor one unit higher.
			if (in.icarus)
			{
				if (!in.hasBottom) return false;
				if (in.midExists && !in.dontPegTop && in.midOffsetDown)
				{
					Span(out, true, in.backCeilingPlusMid, in.backCeilingPlusMid, kLimit, kLimit, false);
					return true;
				}
			}
		}
		if (!in.backSky) Span(out, true, in.zceil[0], in.zceil[1], kLimit, kLimit, true);
		else
		{
			Span(out, true, in.backCeiling[0], in.backCeiling[1], kLimit, kLimit, true);
			out.skyhack = true;
		}
		return true;
	}
	if (in.frontReflect)
	{
		if (in.backReflectSameHeight) return false;   // [rc4l] a line inside one reflective plane adds nothing
	}
	else if (!in.frontPortal || in.samePortal) return false;
	Span(out, true, in.zceil[0], in.zceil[1], kLimit, kLimit, true);
	return true;
}

bool ComputeSkyBottom(const SkyBottomIn &in, SkyWallQuad &out)
{
	if (in.frontSky)
	{
		if (in.backNoSkyDraw) return false;
		// [rc4l] Without a lower texture the sky continues through an open back sector, or one the eye stands below (Vrack2b).
		if (in.bottomNull && (in.backSky ? in.backOpen : in.eyeBelowBackFloor)) return false;
		if (!in.bottomNull || !in.backSky) Span(out, false, -kLimit, -kLimit, in.zfloor[0], in.zfloor[1], true);
		else
		{
			Span(out, false, -kLimit, -kLimit, in.backFloor[0], in.backFloor[1], true);
			out.skyhack = true;
		}
		return true;
	}
	if (in.frontReflect)
	{
		if (in.backReflectSameHeight) return false;
	}
	else if (!in.frontPortal || in.samePortal) return false;
	Span(out, false, -kLimit, -kLimit, in.zfloor[0], in.zfloor[1], true);
	return true;
}

}} // namespace zx::surfaces
