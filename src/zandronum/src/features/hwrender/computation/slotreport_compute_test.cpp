// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include "features/hwrender/computation/slotreport_compute.h"

using namespace zx::hwrender;

namespace {

SlotFacts Slot(int use, const char *name, int w, int h)
{
	SlotFacts f;
	f.use = use;
	f.name = name;
	f.width = w;
	f.height = h;
	return f;
}

SlotFacts Bright(SlotFacts f) { f.brightmap = true; return f; }
SlotFacts Translated(SlotFacts f) { f.translated = true; return f; }

} // namespace

// [Doruk-Aksoy] A family is the first four letters, and a shorter name is its own.
TEST(SlotReport, AFamilyIsTheFirstFourLetters)
{
	EXPECT_EQ("TROO", ComputeSpriteFamily("TROOA1"));
	EXPECT_EQ("PLS", ComputeSpriteFamily("PLS"));
	EXPECT_EQ("", ComputeSpriteFamily(""));
}

// [Doruk-Aksoy] Slots are counted by use, images apart from brightmaps, with a use out of range counted as other.
TEST(SlotReport, SlotsAreCountedByUse)
{
	SlotTally r;
	ComputeSlotReport({ Slot(kSlotUseWorld, "STARTAN2", 64, 128), Bright(Slot(kSlotUseWorld, "STARTAN2", 64, 128)),
		Slot(kSlotUseSkin, "SKIN", 256, 256), Slot(kSlotUseGraphic, "STBAR", 320, 32), Slot(kSlotUseOther, "X", 8, 8),
		Slot(-1, "NEG", 8, 8), Slot(kSlotUseCount, "BIG", 8, 8) }, 2048, 512, r);
	EXPECT_EQ(7, r.slots);
	EXPECT_EQ(1, r.images[kSlotUseWorld]);
	EXPECT_EQ(1, r.brightmaps[kSlotUseWorld]);
	EXPECT_EQ(1, r.images[kSlotUseSkin]);
	EXPECT_EQ(1, r.images[kSlotUseGraphic]);
	EXPECT_EQ(3, r.images[kSlotUseOther]);
	EXPECT_EQ(0, r.images[kSlotUseSprite]);
	EXPECT_TRUE(r.families.empty()) << "only sprites have families";
	EXPECT_EQ(0, r.atlasImages) << "nothing but sprites goes on a page";
}

// [Doruk-Aksoy] Translated, shaded, special and oversize slots are counted, and the atlas takes none but plain sprites at most maxSide a side.
TEST(SlotReport, TheAtlasTakesOnlyPlainSprites)
{
	SlotFacts shaded = Slot(kSlotUseSprite, "FIREA0", 32, 32); shaded.shaded = true;
	SlotFacts special = Slot(kSlotUseSprite, "CAMRA0", 32, 32); special.special = true;
	SlotTally r;
	ComputeSlotReport({ Slot(kSlotUseSprite, "TROOA1", 40, 56), Bright(Slot(kSlotUseSprite, "TROOA1", 40, 56)),
		Translated(Slot(kSlotUseSprite, "TROOB1", 40, 56)), shaded, Bright(shaded), special,
		Slot(kSlotUseSprite, "CYBRA1", 600, 100), Slot(kSlotUseSprite, "CYBRB1", 100, 600) }, 2048, 512, r);
	EXPECT_EQ(1, r.translated);
	EXPECT_EQ(2, r.shaded);
	EXPECT_EQ(1, r.special);
	EXPECT_EQ(2, r.oversize);
	EXPECT_EQ(1, r.atlasImages);
	EXPECT_EQ(1, r.atlasBrightmaps) << "a shaded sprite's brightmap keeps its slot";
	EXPECT_EQ(1, r.atlasTranslated);
	EXPECT_EQ(1, r.atlasPages);
	EXPECT_EQ(1, r.atlasPagesAll);
}

// [Doruk-Aksoy] Pages fill as the atlas fills them, and the translated images take pages of their own count.
TEST(SlotReport, PagesFillAsTheAtlasFillsThem)
{
	std::vector<SlotFacts> slots;
	for (int i = 0; i < 4; i++) slots.push_back(Slot(kSlotUseSprite, "BIGGA0", 62, 62));   // [Doruk-Aksoy] 64 with the gutter, four to a 128 page
	for (int i = 0; i < 4; i++) slots.push_back(Translated(Slot(kSlotUseSprite, "BIGGB0", 62, 62)));
	SlotTally r;
	ComputeSlotReport(slots, 128, 100, r);
	EXPECT_EQ(4, r.atlasImages);
	EXPECT_EQ(4, r.atlasTranslated);
	EXPECT_EQ(1, r.atlasPages);
	EXPECT_EQ(2, r.atlasPagesAll);
}

// [Doruk-Aksoy] An image whose gutter outgrows the page fits nowhere and is left out of the atlas.
TEST(SlotReport, AnImageTooBigForAPageIsLeftOut)
{
	SlotTally r;
	ComputeSlotReport({ Slot(kSlotUseSprite, "WIDEA0", 127, 10), Translated(Slot(kSlotUseSprite, "WIDEB0", 127, 10)) }, 128, 512, r);
	EXPECT_EQ(0, r.atlasImages);
	EXPECT_EQ(0, r.atlasTranslated);
	EXPECT_EQ(0, r.atlasPages);
	EXPECT_EQ(0, r.atlasPagesAll);
	EXPECT_EQ(0, r.ownBytes) << "an image no page takes keeps its own texture, so it saves nothing";
}

// [Doruk-Aksoy] The memory prediction: the atlased sprites' own bytes, brightmap pages wherever a brightmapped image lands, or brightmapped images on pages of their own.
TEST(SlotReport, MemoryIsPredictedForBothLayouts)
{
	std::vector<SlotFacts> slots;
	for (int i = 0; i < 8; i++)
	{
		SlotFacts f = Slot(kSlotUseSprite, "MIXDA0", 62, 62);   // [Doruk-Aksoy] 64 with the gutter, four to a 128 page
		f.hasBrightmap = i % 2 == 0;
		slots.push_back(f);
	}
	slots.push_back(Bright(Slot(kSlotUseSprite, "MIXDA0", 62, 62)));
	SlotTally r;
	ComputeSlotReport(slots, 128, 100, r);
	EXPECT_EQ(9LL * 62 * 62 * 4, r.ownBytes);
	EXPECT_EQ(2, r.atlasPagesAll);
	EXPECT_EQ(2, r.mixedBrightPages) << "every page got a brightmapped image";
	EXPECT_EQ(2, r.groupedColourPages);
	EXPECT_EQ(1, r.groupedBrightPages) << "the four brightmapped images share one page";
}

// [Doruk-Aksoy] The working set counts the last frame's stamps and each window's, adding to what an earlier set counted.
TEST(SlotReport, TheWorkingSetCountsFramesAndWindows)
{
	std::vector<SlotStamp> stamps(5);
	stamps[1] = { 5, 1000 };
	stamps[2] = { 5, 1000 };
	stamps[3] = { 4, 500 };
	stamps[4] = { 2, -3000 };
	const std::vector<double> windows = { 1000, 2000, 5000 };
	std::vector<int> counts;
	ComputeWorkingSet(stamps, 5, 1000, windows, counts);
	EXPECT_EQ((std::vector<int>{ 2, 3, 3, 4 }), counts);
	ComputeWorkingSet({ { 5, 900 } }, 5, 1000, windows, counts);
	EXPECT_EQ((std::vector<int>{ 3, 4, 4, 5 }), counts) << "a second set adds to the first";
}

// [Doruk-Aksoy] Peaks only rise, and grow to as many counts as they are given.
TEST(SlotReport, PeaksOnlyRise)
{
	std::vector<int> peaks;
	ComputeKeepPeaks(peaks, { 3, 4 });
	EXPECT_EQ((std::vector<int>{ 3, 4 }), peaks);
	ComputeKeepPeaks(peaks, { 1, 9, 2 });
	EXPECT_EQ((std::vector<int>{ 3, 9, 2 }), peaks);
}

// [Doruk-Aksoy] The working set's text names each window in seconds or milliseconds, with zeros for counts not yet taken.
TEST(SlotReport, TheWorkingSetTextNamesEachWindow)
{
	EXPECT_EQ("  working set: last frame 3 (peak 3), last 1 s 4 (peak 9), last 500 ms 4 (peak 4); 42 keys since the level began",
		ComputeWorkingSetText({ 3, 4, 4 }, { 3, 9, 4 }, { 1000, 500 }, 42));
	EXPECT_EQ("  working set: last frame 0 (peak 0), last 2 s 0 (peak 0); 0 keys since the level began",
		ComputeWorkingSetText({}, {}, { 2000 }, 0));
}

// [Doruk-Aksoy] Families come most slots first, a tie kept in name order, each with its brightmaps and translated images.
TEST(SlotReport, FamiliesComeMostSlotsFirst)
{
	SlotTally r;
	ComputeSlotReport({ Slot(kSlotUseSprite, "BOSSA1", 8, 8), Slot(kSlotUseSprite, "TROOA1", 8, 8), Bright(Slot(kSlotUseSprite, "TROOA1", 8, 8)),
		Translated(Slot(kSlotUseSprite, "TROOB1", 8, 8)), Slot(kSlotUseSprite, "AAAAA1", 8, 8) }, 2048, 512, r);
	ASSERT_EQ(3u, r.families.size());
	EXPECT_EQ("TROO", r.families[0].name);
	EXPECT_EQ(2, r.families[0].images);
	EXPECT_EQ(1, r.families[0].brightmaps);
	EXPECT_EQ(1, r.families[0].translated);
	EXPECT_EQ("AAAA", r.families[1].name);
	EXPECT_EQ("BOSS", r.families[2].name);
}

// [Doruk-Aksoy] An atlas leaves the slots it does not take and a slot per page, two per page once it takes brightmaps.
TEST(SlotReport, TheAtlasLeavesASlotPerPage)
{
	EXPECT_EQ(100 - 60 + 2, ComputeSlotsWithAtlas(100, 60, 0, 2));
	EXPECT_EQ(100 - 60 - 20 + 4, ComputeSlotsWithAtlas(100, 60, 20, 2));
	EXPECT_EQ(100, ComputeSlotsWithAtlas(100, 0, 0, 0));
}

// [Doruk-Aksoy] The text says what was wanted, what the table holds, what an atlas would leave, and the top families, or none.
TEST(SlotReport, TheTextSaysWhatWasWanted)
{
	SlotTally r;
	ComputeSlotReport({ Slot(kSlotUseWorld, "STARTAN2", 64, 128), Slot(kSlotUseSprite, "TROOA1", 40, 56),
		Bright(Slot(kSlotUseSprite, "TROOA1", 40, 56)), Translated(Slot(kSlotUseSprite, "TROOB1", 40, 56)),
		Slot(kSlotUseSprite, "BOSSA1", 40, 56) }, 2048, 512, r);
	const std::string text = ComputeSlotReportText(r, 4, 4096, 2048, 512, 1);
	EXPECT_NE(std::string::npos, text.find("slots wanted: 5 (4 in the table of 4096, 1 refused once it filled)"));
	EXPECT_NE(std::string::npos, text.find(" world 1 + 0 brightmaps | sprites 3 + 1 brightmaps | skins 0 + 0 brightmaps"));
	EXPECT_NE(std::string::npos, text.find("translated 1, GLDEFS-shaded 0, camera or warp 0, over 512 px 0"));
	EXPECT_NE(std::string::npos, text.find("atlas of 2048 pages: 2 plain sprite images and 1 brightmaps on 1 pages, so 5 slots become 4"));
	EXPECT_NE(std::string::npos, text.find("with translated sprites too: 3 images on 1 pages, so 3"));
	EXPECT_NE(std::string::npos, text.find("memory: their own textures 0 MiB, 0 with mips | pages with brightmapped images mixed in: 1 colour + 0 brightmap = 16 MiB"
		" | brightmapped images on pages of their own: 1 + 0 = 16 MiB"));
	EXPECT_NE(std::string::npos, text.find("sprite families by slots: TROO 3 (1 brightmaps, 1 translated)"));
	EXPECT_EQ(std::string::npos, text.find("BOSS")) << "only the top one";
	EXPECT_NE(std::string::npos, ComputeSlotReportText(r, 4, 4096, 2048, 512, 2).find("TROO 3 (1 brightmaps, 1 translated), BOSS 1"));
	EXPECT_NE(std::string::npos, ComputeSlotReportText(SlotTally(), 0, 4096, 2048, 512, 5).find("sprite families by slots: none"));
	EXPECT_NE(std::string::npos, ComputeSlotReportText(r, 4, 4096, 2048, 512, 0).find("sprite families by slots: none"));
}
