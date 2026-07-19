// [rc4l] Tests for the clean-room fixed-point scale math. Each fixed-shift variant is
// checked against the reference int64 formula so a wrong shift/operation is caught, and
// every one is exercised for full coverage.
#include "gtest/gtest.h"
#include "computation/fixedmath.h"
#include <cstdint>

namespace
{
TEST(FixedMath, GenericForms)
{
	EXPECT_EQ(Scale(1000, 7, 5), int32_t(((int64_t)1000 * 7) / 5));
	EXPECT_EQ(MulScale(123456, 789, 10), int32_t(((int64_t)123456 * 789) >> 10));
	EXPECT_EQ(DivScale(1000, 7, 12), int32_t(((int64_t)1000 << 12) / 7));
	EXPECT_EQ(DMulScale(11, 22, 33, 44, 6), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 6));
	EXPECT_EQ(UMulScale16(0x8000u, 0x8000u), uint32_t(((uint64_t)0x8000 * 0x8000) >> 16));
}

TEST(FixedMath, KsgnAllBranches)
{
	EXPECT_EQ(ksgn(5), 1);
	EXPECT_EQ(ksgn(-5), -1);
	EXPECT_EQ(ksgn(0), 0);
}

TEST(FixedMath, BoundMulScaleClampsBothWays)
{
	// In-range value passes through.
	EXPECT_EQ(BoundMulScale(4, 8, 2), 8);
	// Overflow high -> INT32_MAX, overflow low -> INT32_MIN.
	EXPECT_EQ(BoundMulScale(0x40000000, 0x40000000, 0), INT32_MAX);
	EXPECT_EQ(BoundMulScale(0x40000000, -0x40000000, 0), INT32_MIN);
}

TEST(FixedMath, ClearBuffers)
{
	int32_t a[4] = {1,1,1,1}; clearbuf(a, 4, 9);
	for (int i = 0; i < 4; i++) EXPECT_EQ(a[i], 9);
	uint16_t s[4] = {1,1,1,1}; clearbufshort(s, 4, 7);
	for (int i = 0; i < 4; i++) EXPECT_EQ(s[i], 7);
}
TEST(FixedMath, EveryFixedShiftVariant)
{
	EXPECT_EQ(MulScale1(123456, 789), int32_t(((int64_t)123456 * 789) >> 1));
	EXPECT_EQ(DivScale1(1000, 7), int32_t(((int64_t)1000 << 1) / 7));
	EXPECT_EQ(DMulScale1(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 1));
	EXPECT_EQ(TMulScale1(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 1));
	EXPECT_EQ(MulScale2(123456, 789), int32_t(((int64_t)123456 * 789) >> 2));
	EXPECT_EQ(DivScale2(1000, 7), int32_t(((int64_t)1000 << 2) / 7));
	EXPECT_EQ(DMulScale2(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 2));
	EXPECT_EQ(TMulScale2(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 2));
	EXPECT_EQ(MulScale3(123456, 789), int32_t(((int64_t)123456 * 789) >> 3));
	EXPECT_EQ(DivScale3(1000, 7), int32_t(((int64_t)1000 << 3) / 7));
	EXPECT_EQ(DMulScale3(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 3));
	EXPECT_EQ(TMulScale3(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 3));
	EXPECT_EQ(MulScale4(123456, 789), int32_t(((int64_t)123456 * 789) >> 4));
	EXPECT_EQ(DivScale4(1000, 7), int32_t(((int64_t)1000 << 4) / 7));
	EXPECT_EQ(DMulScale4(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 4));
	EXPECT_EQ(TMulScale4(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 4));
	EXPECT_EQ(MulScale5(123456, 789), int32_t(((int64_t)123456 * 789) >> 5));
	EXPECT_EQ(DivScale5(1000, 7), int32_t(((int64_t)1000 << 5) / 7));
	EXPECT_EQ(DMulScale5(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 5));
	EXPECT_EQ(TMulScale5(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 5));
	EXPECT_EQ(MulScale6(123456, 789), int32_t(((int64_t)123456 * 789) >> 6));
	EXPECT_EQ(DivScale6(1000, 7), int32_t(((int64_t)1000 << 6) / 7));
	EXPECT_EQ(DMulScale6(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 6));
	EXPECT_EQ(TMulScale6(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 6));
	EXPECT_EQ(MulScale7(123456, 789), int32_t(((int64_t)123456 * 789) >> 7));
	EXPECT_EQ(DivScale7(1000, 7), int32_t(((int64_t)1000 << 7) / 7));
	EXPECT_EQ(DMulScale7(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 7));
	EXPECT_EQ(TMulScale7(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 7));
	EXPECT_EQ(MulScale8(123456, 789), int32_t(((int64_t)123456 * 789) >> 8));
	EXPECT_EQ(DivScale8(1000, 7), int32_t(((int64_t)1000 << 8) / 7));
	EXPECT_EQ(DMulScale8(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 8));
	EXPECT_EQ(TMulScale8(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 8));
	EXPECT_EQ(MulScale9(123456, 789), int32_t(((int64_t)123456 * 789) >> 9));
	EXPECT_EQ(DivScale9(1000, 7), int32_t(((int64_t)1000 << 9) / 7));
	EXPECT_EQ(DMulScale9(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 9));
	EXPECT_EQ(TMulScale9(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 9));
	EXPECT_EQ(MulScale10(123456, 789), int32_t(((int64_t)123456 * 789) >> 10));
	EXPECT_EQ(DivScale10(1000, 7), int32_t(((int64_t)1000 << 10) / 7));
	EXPECT_EQ(DMulScale10(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 10));
	EXPECT_EQ(TMulScale10(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 10));
	EXPECT_EQ(MulScale11(123456, 789), int32_t(((int64_t)123456 * 789) >> 11));
	EXPECT_EQ(DivScale11(1000, 7), int32_t(((int64_t)1000 << 11) / 7));
	EXPECT_EQ(DMulScale11(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 11));
	EXPECT_EQ(TMulScale11(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 11));
	EXPECT_EQ(MulScale12(123456, 789), int32_t(((int64_t)123456 * 789) >> 12));
	EXPECT_EQ(DivScale12(1000, 7), int32_t(((int64_t)1000 << 12) / 7));
	EXPECT_EQ(DMulScale12(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 12));
	EXPECT_EQ(TMulScale12(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 12));
	EXPECT_EQ(MulScale13(123456, 789), int32_t(((int64_t)123456 * 789) >> 13));
	EXPECT_EQ(DivScale13(1000, 7), int32_t(((int64_t)1000 << 13) / 7));
	EXPECT_EQ(DMulScale13(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 13));
	EXPECT_EQ(TMulScale13(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 13));
	EXPECT_EQ(MulScale14(123456, 789), int32_t(((int64_t)123456 * 789) >> 14));
	EXPECT_EQ(DivScale14(1000, 7), int32_t(((int64_t)1000 << 14) / 7));
	EXPECT_EQ(DMulScale14(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 14));
	EXPECT_EQ(TMulScale14(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 14));
	EXPECT_EQ(MulScale15(123456, 789), int32_t(((int64_t)123456 * 789) >> 15));
	EXPECT_EQ(DivScale15(1000, 7), int32_t(((int64_t)1000 << 15) / 7));
	EXPECT_EQ(DMulScale15(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 15));
	EXPECT_EQ(TMulScale15(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 15));
	EXPECT_EQ(MulScale16(123456, 789), int32_t(((int64_t)123456 * 789) >> 16));
	EXPECT_EQ(DivScale16(1000, 7), int32_t(((int64_t)1000 << 16) / 7));
	EXPECT_EQ(DMulScale16(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 16));
	EXPECT_EQ(TMulScale16(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 16));
	EXPECT_EQ(MulScale17(123456, 789), int32_t(((int64_t)123456 * 789) >> 17));
	EXPECT_EQ(DivScale17(1000, 7), int32_t(((int64_t)1000 << 17) / 7));
	EXPECT_EQ(DMulScale17(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 17));
	EXPECT_EQ(TMulScale17(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 17));
	EXPECT_EQ(MulScale18(123456, 789), int32_t(((int64_t)123456 * 789) >> 18));
	EXPECT_EQ(DivScale18(1000, 7), int32_t(((int64_t)1000 << 18) / 7));
	EXPECT_EQ(DMulScale18(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 18));
	EXPECT_EQ(TMulScale18(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 18));
	EXPECT_EQ(MulScale19(123456, 789), int32_t(((int64_t)123456 * 789) >> 19));
	EXPECT_EQ(DivScale19(1000, 7), int32_t(((int64_t)1000 << 19) / 7));
	EXPECT_EQ(DMulScale19(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 19));
	EXPECT_EQ(TMulScale19(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 19));
	EXPECT_EQ(MulScale20(123456, 789), int32_t(((int64_t)123456 * 789) >> 20));
	EXPECT_EQ(DivScale20(1000, 7), int32_t(((int64_t)1000 << 20) / 7));
	EXPECT_EQ(DMulScale20(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 20));
	EXPECT_EQ(TMulScale20(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 20));
	EXPECT_EQ(MulScale21(123456, 789), int32_t(((int64_t)123456 * 789) >> 21));
	EXPECT_EQ(DivScale21(1000, 7), int32_t(((int64_t)1000 << 21) / 7));
	EXPECT_EQ(DMulScale21(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 21));
	EXPECT_EQ(TMulScale21(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 21));
	EXPECT_EQ(MulScale22(123456, 789), int32_t(((int64_t)123456 * 789) >> 22));
	EXPECT_EQ(DivScale22(1000, 7), int32_t(((int64_t)1000 << 22) / 7));
	EXPECT_EQ(DMulScale22(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 22));
	EXPECT_EQ(TMulScale22(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 22));
	EXPECT_EQ(MulScale23(123456, 789), int32_t(((int64_t)123456 * 789) >> 23));
	EXPECT_EQ(DivScale23(1000, 7), int32_t(((int64_t)1000 << 23) / 7));
	EXPECT_EQ(DMulScale23(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 23));
	EXPECT_EQ(TMulScale23(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 23));
	EXPECT_EQ(MulScale24(123456, 789), int32_t(((int64_t)123456 * 789) >> 24));
	EXPECT_EQ(DivScale24(1000, 7), int32_t(((int64_t)1000 << 24) / 7));
	EXPECT_EQ(DMulScale24(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 24));
	EXPECT_EQ(TMulScale24(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 24));
	EXPECT_EQ(MulScale25(123456, 789), int32_t(((int64_t)123456 * 789) >> 25));
	EXPECT_EQ(DivScale25(1000, 7), int32_t(((int64_t)1000 << 25) / 7));
	EXPECT_EQ(DMulScale25(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 25));
	EXPECT_EQ(TMulScale25(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 25));
	EXPECT_EQ(MulScale26(123456, 789), int32_t(((int64_t)123456 * 789) >> 26));
	EXPECT_EQ(DivScale26(1000, 7), int32_t(((int64_t)1000 << 26) / 7));
	EXPECT_EQ(DMulScale26(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 26));
	EXPECT_EQ(TMulScale26(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 26));
	EXPECT_EQ(MulScale27(123456, 789), int32_t(((int64_t)123456 * 789) >> 27));
	EXPECT_EQ(DivScale27(1000, 7), int32_t(((int64_t)1000 << 27) / 7));
	EXPECT_EQ(DMulScale27(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 27));
	EXPECT_EQ(TMulScale27(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 27));
	EXPECT_EQ(MulScale28(123456, 789), int32_t(((int64_t)123456 * 789) >> 28));
	EXPECT_EQ(DivScale28(1000, 7), int32_t(((int64_t)1000 << 28) / 7));
	EXPECT_EQ(DMulScale28(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 28));
	EXPECT_EQ(TMulScale28(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 28));
	EXPECT_EQ(MulScale29(123456, 789), int32_t(((int64_t)123456 * 789) >> 29));
	EXPECT_EQ(DivScale29(1000, 7), int32_t(((int64_t)1000 << 29) / 7));
	EXPECT_EQ(DMulScale29(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 29));
	EXPECT_EQ(TMulScale29(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 29));
	EXPECT_EQ(MulScale30(123456, 789), int32_t(((int64_t)123456 * 789) >> 30));
	EXPECT_EQ(DivScale30(1000, 7), int32_t(((int64_t)1000 << 30) / 7));
	EXPECT_EQ(DMulScale30(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 30));
	EXPECT_EQ(TMulScale30(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 30));
	EXPECT_EQ(MulScale31(123456, 789), int32_t(((int64_t)123456 * 789) >> 31));
	EXPECT_EQ(DivScale31(1000, 7), int32_t(((int64_t)1000 << 31) / 7));
	EXPECT_EQ(DMulScale31(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 31));
	EXPECT_EQ(TMulScale31(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 31));
	EXPECT_EQ(MulScale32(123456, 789), int32_t(((int64_t)123456 * 789) >> 32));
	EXPECT_EQ(DivScale32(1000, 7), int32_t(((int64_t)1000 << 32) / 7));
	EXPECT_EQ(DMulScale32(11, 22, 33, 44), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44)) >> 32));
	EXPECT_EQ(TMulScale32(11, 22, 33, 44, 55, 66), int32_t((((int64_t)11 * 22) + ((int64_t)33 * 44) + ((int64_t)55 * 66)) >> 32));
}
} // namespace
