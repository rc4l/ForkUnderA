// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-replication/computation/mapstate_compute.h"

namespace zx { namespace netreplication {

unsigned ComputeSectorChanges(const SectorLook &was, const SectorLook &now)
{
	unsigned bits = 0;
	if (was.floorPic != now.floorPic || was.ceilingPic != now.ceilingPic) bits |= kSectorFlat;
	if (was.color != now.color || was.desaturate != now.desaturate) bits |= kSectorColor;
	if (was.fade != now.fade) bits |= kSectorFade;
	if (was.gravity != now.gravity) bits |= kSectorGravity;
	if (was.friction != now.friction || was.moveFactor != now.moveFactor) bits |= kSectorFriction;
	if (was.reflectFloor != now.reflectFloor || was.reflectCeiling != now.reflectCeiling) bits |= kSectorReflection;
	if (was.xScaleFloor != now.xScaleFloor || was.yScaleFloor != now.yScaleFloor || was.xScaleCeiling != now.xScaleCeiling ||
		was.yScaleCeiling != now.yScaleCeiling)
		bits |= kSectorScale;
	if (was.angleFloor != now.angleFloor || was.angleCeiling != now.angleCeiling) bits |= kSectorRotation;
	if (was.special != now.special) bits |= kSectorSpecial;
	return bits;
}

SectorLook ComputeTakeFamilies(const SectorLook &sent, const SectorLook &now, unsigned families)
{
	SectorLook out = sent;
	if (families & kSectorFlat) { out.floorPic = now.floorPic; out.ceilingPic = now.ceilingPic; }
	if (families & kSectorColor) { out.color = now.color; out.desaturate = now.desaturate; }
	if (families & kSectorFade) out.fade = now.fade;
	if (families & kSectorGravity) out.gravity = now.gravity;
	if (families & kSectorFriction) { out.friction = now.friction; out.moveFactor = now.moveFactor; }
	if (families & kSectorReflection) { out.reflectFloor = now.reflectFloor; out.reflectCeiling = now.reflectCeiling; }
	if (families & kSectorScale)
	{
		out.xScaleFloor = now.xScaleFloor; out.yScaleFloor = now.yScaleFloor;
		out.xScaleCeiling = now.xScaleCeiling; out.yScaleCeiling = now.yScaleCeiling;
	}
	if (families & kSectorRotation) { out.angleFloor = now.angleFloor; out.angleCeiling = now.angleCeiling; }
	if (families & kSectorSpecial) out.special = now.special;
	return out;
}

LineLook ComputeTakeFamilies(const LineLook &sent, const LineLook &now, unsigned families)
{
	LineLook out = sent;
	if (families & kLineAlpha) out.alpha = now.alpha;
	if (families & kLineFlags) out.clientFlags = now.clientFlags;
	for (int i = 0; i < 6; i++)
		if (families & (kLineTexture << i)) out.textures[i] = now.textures[i];
	return out;
}

unsigned ComputeLineChanges(const LineLook &was, const LineLook &now)
{
	unsigned bits = 0;
	if (was.alpha != now.alpha) bits |= kLineAlpha;
	if (was.clientFlags != now.clientFlags) bits |= kLineFlags;
	for (int i = 0; i < 6; i++)
		if (was.textures[i] != now.textures[i]) bits |= kLineTexture << i;
	return bits;
}

unsigned ComputeLineTextureParts(unsigned lineChanges)
{
	return (lineChanges / kLineTexture) & 0x3F;
}

}} // namespace zx::netreplication
