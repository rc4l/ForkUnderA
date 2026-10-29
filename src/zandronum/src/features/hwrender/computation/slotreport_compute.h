// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] What a level's bindless material slots hold by kind, and how many an atlas of sprite pages would free, for fua_dg_slotreport.

#ifndef ZX_SLOTREPORT_COMPUTE_H
#define ZX_SLOTREPORT_COMPUTE_H

#include <string>
#include <vector>

namespace zx { namespace hwrender {

// [Doruk-Aksoy] What a slot's texture is for, in zx::materials::MaterialUse's order.
enum SlotUse { kSlotUseWorld, kSlotUseSprite, kSlotUseSkin, kSlotUseGraphic, kSlotUseOther, kSlotUseCount };

// [Doruk-Aksoy] One slot asked for: its texture's use, size and name, whether it holds the texture's brightmap, a translation, a GLDEFS shader or a camera texture or warp, and whether an image's texture has a brightmap.
struct SlotFacts
{
	int use = kSlotUseOther;
	bool brightmap = false, translated = false, shaded = false, special = false, hasBrightmap = false;
	int width = 0, height = 0;
	std::string name;
};

// [Doruk-Aksoy] A sprite family's slots: its name's first four letters, its images, its brightmaps and how many of the images are translated.
struct SlotFamily
{
	std::string name;
	int images = 0, brightmaps = 0, translated = 0;
};

// [Doruk-Aksoy] A set of slots counted: by use, by what keeps them off a page, and what an atlas would take.
struct SlotTally
{
	int slots = 0;
	int images[kSlotUseCount] = {}, brightmaps[kSlotUseCount] = {};
	int translated = 0, shaded = 0, special = 0, oversize = 0;
	// [Doruk-Aksoy] Untranslated plain sprite images an atlas takes as it is, the brightmap slots of plain sprites, and the pages the images fill.
	int atlasImages = 0, atlasBrightmaps = 0, atlasPages = 0;
	// [Doruk-Aksoy] The translated plain sprite images it could take as well, and the pages all of them fill.
	int atlasTranslated = 0, atlasPagesAll = 0;
	// [Doruk-Aksoy] The bytes those sprites' own textures take without mips, then the pages each layout needs: brightmapped images mixed with the rest, or on colour pages of their own.
	long long ownBytes = 0;
	int mixedBrightPages = 0, groupedColourPages = 0, groupedBrightPages = 0;
	std::vector<SlotFamily> families;   // [Doruk-Aksoy] by slots, most first, then by name
};

// [Doruk-Aksoy] When a dynamic piece last asked for a slot: the frame and the time in ms, frame -1 for never.
struct SlotStamp
{
	long long frame = -1;
	double ms = 0;
};

// [Doruk-Aksoy] Adds to counts (sized here to one more than windowsMs) the stamps of `frame`, then those within each window of the last windowsMs[i] ms up to nowMs.
void ComputeWorkingSet(const std::vector<SlotStamp> &stamps, long long frame, double nowMs, const std::vector<double> &windowsMs, std::vector<int> &counts);

// [Doruk-Aksoy] Raises each peak to its count, growing peaks to as many counts as there are.
void ComputeKeepPeaks(std::vector<int> &peaks, const std::vector<int> &counts);

// [Doruk-Aksoy] The working set as text: the last frame's count and each window's, each with its peak, and the keys asked for since the level began.
std::string ComputeWorkingSetText(const std::vector<int> &now, const std::vector<int> &peaks, const std::vector<double> &windowsMs, int keys);

// [Doruk-Aksoy] A sprite's family: the first four letters of its name.
std::string ComputeSpriteFamily(const std::string &name);

// [Doruk-Aksoy] The report on slots, an image eligible for the atlas being a plain sprite of at most maxSide a side, packed with its gutter on pageSize pages as the atlas packs them.
void ComputeSlotReport(const std::vector<SlotFacts> &slots, int pageSize, int maxSide, SlotTally &out);

// [Doruk-Aksoy] The slots an atlas leaves: the ones it does not take, a slot per page, and a brightmap page per page when it takes any brightmaps.
int ComputeSlotsWithAtlas(int slots, int images, int brightmaps, int pages);

// [Doruk-Aksoy] The report as text: inTable of its slots in the table of capacity, the rest refused once it filled, and the top sprite families.
std::string ComputeSlotReportText(const SlotTally &r, int inTable, int capacity, int pageSize, int maxSide, int top);

}} // namespace zx::hwrender

#endif // ZX_SLOTREPORT_COMPUTE_H
