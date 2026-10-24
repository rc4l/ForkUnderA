// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The map state only a server writes, read once a tic and compared with what its clients were last sent, so each family goes out from one place when it changes.

#ifndef ZX_MAPSTATE_COMPUTE_H
#define ZX_MAPSTATE_COMPUTE_H

#include <cstdint>

namespace zx { namespace netreplication {

// [rc4l] A sector's server-written look: flats, colour, fade, gravity, friction, reflection, scale, rotation and special.
struct SectorLook
{
	int floorPic = 0, ceilingPic = 0;
	uint32_t color = 0, fade = 0;   // [rc4l] rgb
	int desaturate = 0;
	float gravity = 0;
	int64_t friction = 0, moveFactor = 0;
	float reflectFloor = 0, reflectCeiling = 0;
	int64_t xScaleFloor = 0, yScaleFloor = 0, xScaleCeiling = 0, yScaleCeiling = 0;
	uint32_t angleFloor = 0, angleCeiling = 0;
	int special = 0;
};

// [rc4l] One bit per family a server sends of a sector, each with its own command.
enum SectorFamily : unsigned
{
	kSectorFlat = 1 << 0,
	kSectorColor = 1 << 1,
	kSectorFade = 1 << 2,
	kSectorGravity = 1 << 3,
	kSectorFriction = 1 << 4,
	kSectorReflection = 1 << 5,
	kSectorScale = 1 << 6,
	kSectorRotation = 1 << 7,
	kSectorSpecial = 1 << 8,
};

// [rc4l] The families of a sector that differ between what was sent and what it is now.
unsigned ComputeSectorChanges(const SectorLook &was, const SectorLook &now);

// [rc4l] The sector as sent once the given families of now went out another way (one command for a whole tag), the rest left as they were sent.
SectorLook ComputeTakeFamilies(const SectorLook &sent, const SectorLook &now, unsigned families);

// [rc4l] A line's server-written look: its translucency, the flags clients move and draw by, and its sides' six textures (front top, mid, bottom, then back).
struct LineLook
{
	int64_t alpha = 0;
	uint32_t clientFlags = 0;
	int textures[6] = {0, 0, 0, 0, 0, 0};
};

// [rc4l] A line's families: alpha, flags, and one bit per texture from kLineTexture up, in LineLook::textures' order (TEXCHANGE_*'s order too).
enum LineFamily : unsigned
{
	kLineAlpha = 1 << 0,
	kLineFlags = 1 << 1,
	kLineTexture = 1 << 2,
};

unsigned ComputeLineChanges(const LineLook &was, const LineLook &now);

// [rc4l] The line as sent once the given families of now went out another way (one command for a whole line id).
LineLook ComputeTakeFamilies(const LineLook &sent, const LineLook &now, unsigned families);

// [rc4l] The textures of a line's changes as TEXCHANGE_* bits, bit i for LineLook::textures[i].
unsigned ComputeLineTextureParts(unsigned lineChanges);

}} // namespace zx::netreplication

#endif // ZX_MAPSTATE_COMPUTE_H
