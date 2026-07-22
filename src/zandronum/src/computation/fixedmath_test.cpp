// [rc4l] Tests for the widened (64-bit) fixed-point scale math. fixedmath.h now delegates to
// the tested zx:: scale layer; these checks confirm each named form maps to the right
// operation/shift and returns the full 64-bit result, and exercise every numbered variant for
// coverage. References use __int128 so the 64-bit results are exact.
#include "gtest/gtest.h"
#include "computation/fixedmath.h"
#include <cstdint>

namespace
{
TEST(FixedMath, GenericForms)
{
	EXPECT_EQ(Scale(1000, 7, 5), (int64_t)(((__int128)1000 * 7) / 5));
	EXPECT_EQ(MulScale(123456, 789, 10), (int64_t)(((__int128)123456 * 789) >> 10));
	EXPECT_EQ(DivScale(1000, 7, 12), (int64_t)(((__int128)1000 * (1 << 12)) / 7));
	EXPECT_EQ(DMulScale(11, 22, 33, 44, 6), (int64_t)((((__int128)11 * 22) + ((__int128)33 * 44)) >> 6));
	EXPECT_EQ(UMulScale16(0x8000u, 0x8000u), uint32_t(((uint64_t)0x8000 * 0x8000) >> 16));
	// [rc4l] Widened Scale carries a giant product that a 32-bit intermediate would truncate.
	const int64_t big = 1LL << 40;
	EXPECT_EQ(Scale(big, 1000, 7), (int64_t)(((__int128)big * 1000) / 7));
}

TEST(FixedMath, KsgnAllBranches)
{
	EXPECT_EQ(ksgn(5), 1);
	EXPECT_EQ(ksgn(-5), -1);
	EXPECT_EQ(ksgn(0), 0);
}

TEST(FixedMath, BoundMulScaleCarriesFullValue)
{
	// [rc4l] Now 64-bit: the product that used to clamp to INT32_MAX is carried in full.
	EXPECT_EQ(BoundMulScale(4, 8, 2), 8);
	EXPECT_EQ(BoundMulScale(0x40000000, 0x40000000, 0), (int64_t)((__int128)0x40000000 * 0x40000000));
}

TEST(FixedMath, ClearBuffers)
{
	int32_t a[4] = {1,1,1,1}; clearbuf(a, 4, 9);
	for (int i = 0; i < 4; i++) EXPECT_EQ(a[i], 9);
	uint16_t s[4] = {1,1,1,1}; clearbufshort(s, 4, 7);
	for (int i = 0; i < 4; i++) EXPECT_EQ(s[i], 7);
}

// [rc4l] Every numbered fixed-shift form, against the exact __int128 reference. Positive
// inputs keep the divide/left-shift references well-defined.
#define ZX_CHECK_N(N) \
	EXPECT_EQ(MulScale##N(123456, 789), (int64_t)(((__int128)123456 * 789) >> N)); \
	EXPECT_EQ(DivScale##N(1000, 7), (int64_t)(((__int128)1000 * ((__int128)1 << N)) / 7)); \
	EXPECT_EQ(DMulScale##N(11, 22, 33, 44), (int64_t)((((__int128)11 * 22) + ((__int128)33 * 44)) >> N)); \
	EXPECT_EQ(TMulScale##N(11, 22, 33, 44, 55, 66), (int64_t)((((__int128)11 * 22) + ((__int128)33 * 44) + ((__int128)55 * 66)) >> N));

TEST(FixedMath, EveryFixedShiftVariant)
{
	ZX_CHECK_N(1)  ZX_CHECK_N(2)  ZX_CHECK_N(3)  ZX_CHECK_N(4)
	ZX_CHECK_N(5)  ZX_CHECK_N(6)  ZX_CHECK_N(7)  ZX_CHECK_N(8)
	ZX_CHECK_N(9)  ZX_CHECK_N(10) ZX_CHECK_N(11) ZX_CHECK_N(12)
	ZX_CHECK_N(13) ZX_CHECK_N(14) ZX_CHECK_N(15) ZX_CHECK_N(16)
	ZX_CHECK_N(17) ZX_CHECK_N(18) ZX_CHECK_N(19) ZX_CHECK_N(20)
	ZX_CHECK_N(21) ZX_CHECK_N(22) ZX_CHECK_N(23) ZX_CHECK_N(24)
	ZX_CHECK_N(25) ZX_CHECK_N(26) ZX_CHECK_N(27) ZX_CHECK_N(28)
	ZX_CHECK_N(29) ZX_CHECK_N(30) ZX_CHECK_N(31) ZX_CHECK_N(32)
}
#undef ZX_CHECK_N

// [rc4l] The widened forms carry giant operands the old 32-bit intermediate would truncate.
TEST(FixedMath, WidenedFormsCarryGiantOperands)
{
	const int64_t big = 1LL << 40;
	EXPECT_EQ(MulScale16(big, 3), (int64_t)(((__int128)big * 3) >> 16));
	EXPECT_EQ(DMulScale16(big, 3, 5, 7), (int64_t)((((__int128)big * 3) + ((__int128)5 * 7)) >> 16));
}
} // namespace
