// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which sky a sector shows, as GL's GLWall::SkyPlane fills a GLSkyInfo: MBF's sky taken from a line, sky2, and the swapped and double skies of a level.

#ifndef ZX_SKYLOOK_COMPUTE_H
#define ZX_SKYLOOK_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] r_defs.h's PL_SKYFLAT: on its own a sector shows sky2, with a line number plus one under it that line's sky.
const int kSkyFlatFlag = 0x40000000;

// [rc4l] One sky: its texture (an FTextureID index) turned xOffset degrees, raised yOffset texels and mirrored, and a double sky's second layer drawn over it (secondTexture -1 for none).
struct SkyLook
{
	int texture;
	float xOffset, yOffset;
	bool mirrored;
	int secondTexture;
	float secondXOffset;
};

// [rc4l] What GL reads to pick a sector's sky.
struct SkyLookIn
{
	int sectorSky;                     // [rc4l] sector_t::sky
	// [rc4l] The transfer line's front sidedef: its upper and lower textures, whether each is a valid id and whether it draws (not the null texture), their offsets (x in degrees), and !args[2].
	int lineTop, lineBottom;
	bool lineTopDraws, lineBottomValid, lineBottomDraws;
	float lineTopX, lineTopY, lineBottomX, lineBottomY;
	bool lineMirrored;
	bool swapSkies, doubleSky;         // [rc4l] LEVEL_SWAPSKIES (lightning) and LEVEL_DOUBLESKY
	int sky1, sky2;                    // [rc4l] sky1texture and sky2texture
	float sky1Pos, sky2Pos;            // [rc4l] their scroll, in degrees
};

// [rc4l] The sky a sector shows: the line's texture when the line gives one that draws, else sky2 where the level or sector asks for it and it differs from sky1, else sky1, under sky1 again as a double sky's second layer.
SkyLook ComputeSkyLook(const SkyLookIn &in);

// [rc4l] Whether two skies draw the same picture, so one picture serves both.
bool ComputeSameSkyLook(const SkyLook &a, const SkyLook &b);

// [rc4l] The picture of `count` in `looks` (at most `room`) that a pass's sky `look` paints from: -1 for the level's own sky, else an equal look's, else a new one appended while there is room, else -1.
int ComputeSkyLookPicture(SkyLook *looks, int &count, int room, const SkyLook &look, const SkyLook &level);

// [rc4l] A sector's sky as the small number its sky quads carry in their texture u: 0 the level's, 1 sky2, a line's that line plus 2; and back.
int ComputeSkyQuadKey(int sectorSky);
int ComputeSkyFromQuadKey(int key);

// [rc4l] One dome draw's constants as RenderDome's matrices give them: its turn in radians (-180 degrees plus xOffset), u's sign (-1 mirrored), v's raise (yOffset texels of a texh-high texture at the dome's v scale), and 1 for a double sky's second layer.
void ComputeSkyDrawConstants(float xOffsetDeg, bool mirrored, float yOffset, float vscale, int texh, bool secondLayer, float out[4]);

}} // namespace zx::hwrender

#endif // ZX_SKYLOOK_COMPUTE_H
