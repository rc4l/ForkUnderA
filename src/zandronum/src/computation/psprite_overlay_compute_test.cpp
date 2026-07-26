// [MGOOOOOO] Tests for the pure A_Overlay decision/offset math. Every branch of every
// helper is exercised so the coverage gate stays at 100% for this unit.
#include "gtest/gtest.h"
#include "computation/psprite_overlay_compute.h"

namespace
{
TEST(PSpriteOverlay, ReservedLayers)
{
	EXPECT_TRUE(ComputeIsReservedPSpriteLayer(ZX_PSP_WEAPON));
	EXPECT_TRUE(ComputeIsReservedPSpriteLayer(ZX_PSP_FLASH));
	EXPECT_TRUE(ComputeIsReservedPSpriteLayer(ZX_PSP_TARGETCENTER));
	EXPECT_TRUE(ComputeIsReservedPSpriteLayer(ZX_PSP_TARGETLEFT));
	EXPECT_TRUE(ComputeIsReservedPSpriteLayer(ZX_PSP_TARGETRIGHT));
	// Overlay ids (including negative and zero) are never reserved.
	EXPECT_FALSE(ComputeIsReservedPSpriteLayer(0));
	EXPECT_FALSE(ComputeIsReservedPSpriteLayer(2));
	EXPECT_FALSE(ComputeIsReservedPSpriteLayer(-5));
	EXPECT_FALSE(ComputeIsReservedPSpriteLayer(999));
}

TEST(PSpriteOverlay, SortedInsertIndex)
{
	const int layers[] = { 1, 5, 1000 };
	// Before the front, in the middle, and past the end.
	EXPECT_EQ(ComputeSortedInsertIndex(layers, 3, -3), 0u);
	EXPECT_EQ(ComputeSortedInsertIndex(layers, 3, 3), 1u);
	EXPECT_EQ(ComputeSortedInsertIndex(layers, 3, 900), 2u);
	EXPECT_EQ(ComputeSortedInsertIndex(layers, 3, 5000), 3u);
	// Empty array and null both yield index 0.
	EXPECT_EQ(ComputeSortedInsertIndex(layers, 0, 42), 0u);
	EXPECT_EQ(ComputeSortedInsertIndex(nullptr, 0, 42), 0u);
}

TEST(PSpriteOverlay, ClearOverlayShouldRemove)
{
	// Safety protects reserved layers regardless of range.
	EXPECT_FALSE(ComputeClearOverlayShouldRemove(ZX_PSP_WEAPON, 0, 0, true));
	EXPECT_FALSE(ComputeClearOverlayShouldRemove(ZX_PSP_FLASH, -10, 5000, true));
	// Without safety, reserved layers are eligible.
	EXPECT_TRUE(ComputeClearOverlayShouldRemove(ZX_PSP_WEAPON, 0, 0, false));
	// start==stop==0 clears every non-reserved layer.
	EXPECT_TRUE(ComputeClearOverlayShouldRemove(2, 0, 0, true));
	EXPECT_TRUE(ComputeClearOverlayShouldRemove(-3, 0, 0, true));
	// Explicit range is inclusive; outside it is kept.
	EXPECT_TRUE(ComputeClearOverlayShouldRemove(5, 5, 10, true));
	EXPECT_TRUE(ComputeClearOverlayShouldRemove(10, 5, 10, true));
	EXPECT_FALSE(ComputeClearOverlayShouldRemove(4, 5, 10, true));
	EXPECT_FALSE(ComputeClearOverlayShouldRemove(11, 5, 10, true));
}

TEST(PSpriteOverlay, OverlayFlags)
{
	// Set ORs bits in; clear ANDs them out; unrelated bits are preserved.
	EXPECT_EQ(ComputeOverlayFlags(0x1u, 0x2u, true), 0x3u);
	EXPECT_EQ(ComputeOverlayFlags(0x3u, 0x2u, false), 0x1u);
	EXPECT_EQ(ComputeOverlayFlags(0x5u, 0x2u, true), 0x7u);
	EXPECT_EQ(ComputeOverlayFlags(0x5u, 0x4u, false), 0x1u);
	// Setting an already-set bit / clearing an absent bit are no-ops.
	EXPECT_EQ(ComputeOverlayFlags(0x2u, 0x2u, true), 0x2u);
	EXPECT_EQ(ComputeOverlayFlags(0x2u, 0x4u, false), 0x2u);
}

TEST(PSpriteOverlay, OverlayAxis)
{
	// keep wins over add.
	EXPECT_EQ(ComputeOverlayAxis(7, 3, true, false), 7);
	EXPECT_EQ(ComputeOverlayAxis(7, 3, true, true), 7);
	// add sums, otherwise replace.
	EXPECT_EQ(ComputeOverlayAxis(7, 3, false, true), 10);
	EXPECT_EQ(ComputeOverlayAxis(7, 3, false, false), 3);
}

TEST(PSpriteOverlay, SquareScaleY)
{
	// scaleY == 0 copies scaleX; otherwise scaleY is kept.
	EXPECT_EQ(ComputeOverlaySquareScaleY(65536, 0), 65536);
	EXPECT_EQ(ComputeOverlaySquareScaleY(65536, 131072), 131072);
	EXPECT_EQ(ComputeOverlaySquareScaleY(0, 0), 0);
}

TEST(PSpriteOverlay, DrawOffset)
{
	int64_t ax = -1;
	int64_t ay = -1;

	// Reserved weapon/flash layers always ride the bob and ignore flags.
	ComputeOverlayDrawOffset(true, ZX_PSPF_ADDWEAPON | ZX_PSPF_ADDBOB, 50, 60, 3, 4, &ax, &ay);
	EXPECT_EQ(ax, 3);
	EXPECT_EQ(ay, 4);

	// Overlay with no flags gets nothing added.
	ComputeOverlayDrawOffset(false, 0, 50, 60, 3, 4, &ax, &ay);
	EXPECT_EQ(ax, 0);
	EXPECT_EQ(ay, 0);

	// ADDWEAPON adds the weapon offset only.
	ComputeOverlayDrawOffset(false, ZX_PSPF_ADDWEAPON, 50, 60, 3, 4, &ax, &ay);
	EXPECT_EQ(ax, 50);
	EXPECT_EQ(ay, 60);

	// ADDBOB adds the bob only.
	ComputeOverlayDrawOffset(false, ZX_PSPF_ADDBOB, 50, 60, 3, 4, &ax, &ay);
	EXPECT_EQ(ax, 3);
	EXPECT_EQ(ay, 4);

	// Both flags add both.
	ComputeOverlayDrawOffset(false, ZX_PSPF_ADDWEAPON | ZX_PSPF_ADDBOB, 50, 60, 3, 4, &ax, &ay);
	EXPECT_EQ(ax, 53);
	EXPECT_EQ(ay, 64);

	// Null out-pointers are ignored (no crash).
	ComputeOverlayDrawOffset(false, 0, 1, 2, 3, 4, nullptr, nullptr);
}
} // namespace
