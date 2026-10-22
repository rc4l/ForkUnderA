// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A sector's light as the GPU's sector light table holds it, and the shading inputs a wall or flat lit by that sector reads from it, so a flicker rewrites one entry instead of every surface it lights.

#ifndef ZX_SECTORLIGHT_COMPUTE_H
#define ZX_SECTORLIGHT_COMPUTE_H

namespace zx { namespace levelmesh {

// [rc4l] One sector's entry: its light level, its floor and ceiling lights as gl_ClampLight(GetFloorLight()) gives them, and its colormap.
struct SectorLightEntry
{
	int lightLevel = 0, floorLight = 0, ceilingLight = 0;
	unsigned lightColor = 0, fadeColor = 0;   // [rc4l] FColormap::LightColor and FadeColor, packed
	int blendFactor = 0, desaturation = 0;
};

// [rc4l] Which of a sector's lights a surface takes.
enum SectorLitKind { kSectorLitFloor = 0, kSectorLitCeiling = 1, kSectorLitWall = 2 };

// [rc4l] Where a surface's light comes from: its sector and kind, and for a wall its sidedef's own light, whether that is absolute or kept in fog (WALLF_ABSLIGHTING, WALLF_LIGHT_FOG), and its fake contrast as it is out of fog.
struct SectorLightRef
{
	int sector = -1;
	int kind = kSectorLitFloor;
	int sideLight = 0;
	bool sideAbsolute = false, sideLightFog = false;
	int contrast = 0;
};

// [rc4l] The inputs to ComputeShading a reference reads, before the player's extra light.
struct SectorLitInput
{
	int lightLevel = 0, relLight = 0;
	unsigned lightColor = 0, fadeColor = 0;
	int blendFactor = 0, desaturation = 0;
};

// [rc4l] A flat's light is its plane's; a wall's is BuildDerivedWallLight's over side_t::SideLightLevel, the sector's light clamped, the side's added unless absolute or fogged, and the fake contrast dropped in fog and bounded by GLWall::Process's rule.
SectorLitInput ComputeSectorLitInput(const SectorLightEntry &e, const SectorLightRef &r, bool fadeTable);

// [rc4l] Whether in is exactly what a surface was shaded from, its relative light without the player's extra light, so the surface may be lit from the table without looking any different.
bool ComputeSectorLitReproduces(const SectorLitInput &in, int lightLevel, int relLight, unsigned lightColor, unsigned fadeColor, int blendFactor, int desaturation);

// [rc4l] The 32-bit words an entry takes in the GPU's table, read by fuaSectorLit (dgshared.h).
enum { kSectorLightWords = 8 };
void ComputeSectorLightWords(const SectorLightEntry &e, unsigned out[kSectorLightWords]);

}} // namespace zx::levelmesh

#endif // ZX_SECTORLIGHT_COMPUTE_H
