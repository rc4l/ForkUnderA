// [rc4l] Regression test for the "moving forward forces you south" bug.
//
// The finesine[] wraparound tail was copied with a byte size tied to angle_t (4 bytes). Once
// fixed_t widened to 64 bits that copy moved only half the tail, zeroing finecosine across the
// ~315-360 degree arc. A player facing that arc and pressing forward then got
// velx = FixedMul(move, finecosine) = FixedMul(move, 0) = 0, while vely stayed negative -- so
// they slid due south no matter which way they faced. This pins both the mechanism and the fix.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "features/fixed64/computation/finetable_compute.h"

namespace
{
constexpr int FINEANGLES = 8192;            // matches tables.h
constexpr int64_t FRACUNIT = int64_t(1) << 16;
constexpr double kPi = 3.14159265358979323846;

// [rc4l] Build finesine[0 .. FINEANGLES) exactly as R_InitTables does, in a 64-bit-wide table
// with room for the FINEANGLES/4 wraparound tail (left zeroed for the caller to fill).
std::vector<int64_t> BuildBaseSineTable()
{
	std::vector<int64_t> t(5 * FINEANGLES / 4, 0);
	const double pimul = kPi * 2 / FINEANGLES;
	for (int i = 0; i < FINEANGLES / 4; i++)
		t[i] = int64_t(FRACUNIT * std::sin(i * pimul));
	for (int i = 0; i < FINEANGLES / 4; i++)
		t[i + FINEANGLES / 4] = t[FINEANGLES / 4 - 1 - i];
	for (int i = 0; i < FINEANGLES / 2; i++)
		t[i + FINEANGLES / 2] = -t[i];
	t[FINEANGLES / 4] = FRACUNIT;
	t[FINEANGLES * 3 / 4] = -FRACUNIT;
	return t;
}

// finecosine is finesine read with a +FINEANGLES/4 phase shift (see tables.h cosine_inline).
int64_t Cosine(const std::vector<int64_t> &t, int k) { return t[k + FINEANGLES / 4]; }

// FixedMul, the 16.16 multiply the thrust code uses; operands here are well within int32.
int64_t FixedMul(int64_t a, int64_t b) { return (a * b) >> 16; }

// A fine index squarely inside the arc the bug zeroed (315-360 deg): ~351 deg, i.e. facing
// almost due east. The reporter estimated onset near -5/10 deg -- the same neighbourhood.
constexpr int kSouthArcIndex = 8000;
} // namespace

// [rc4l] The pre-fix sizing (sizeof(angle_t) == 4 bytes) reproduced verbatim on the widened
// table: it undercopies the tail, so finecosine over the arc reads back zero and a forward
// thrust produces NO eastward velocity -- only the negative (south) sine component survives.
// This is the bug, pinned.
TEST(FineTable, AngleWidthCopyZeroesEastwardThrust)
{
	auto t = BuildBaseSineTable();
	std::memcpy(&t[FINEANGLES], &t[0], sizeof(uint32_t) * (FINEANGLES / 4)); // the original bug

	// cos(351 deg) is strongly positive, so finecosine here SHOULD be large; the bug makes it 0.
	EXPECT_EQ(Cosine(t, kSouthArcIndex), 0) << "pre-fix: finecosine zeroed across the south arc";

	const int64_t move = FRACUNIT; // one unit of forward thrust
	const int64_t velx = FixedMul(move, Cosine(t, kSouthArcIndex));
	const int64_t vely = FixedMul(move, t[kSouthArcIndex]);
	EXPECT_EQ(velx, 0) << "pre-fix: no eastward component -> player is shoved off-axis";
	EXPECT_LT(vely, 0) << "pre-fix: only the southward component remains";
}

// [rc4l] The fix: fill the tail via the helper, which sizes the copy off the element type.
// finecosine now tracks cos across the FULL circle, so a forward thrust at the same facing is
// dominated by the (correct) eastward component -- no phantom south slide.
TEST(FineTable, ElementWidthCopyRestoresEastwardThrust)
{
	auto t = BuildBaseSineTable();
	zx::FillFineSineWrap(t.data(), FINEANGLES);

	// finecosine[8000] ~= cos(351.6 deg) * FRACUNIT ~= +0.989 * 65536.
	EXPECT_GT(Cosine(t, kSouthArcIndex), FRACUNIT * 9 / 10) << "post-fix: strong eastward cosine";

	const int64_t move = FRACUNIT;
	const int64_t velx = FixedMul(move, Cosine(t, kSouthArcIndex));
	const int64_t vely = FixedMul(move, t[kSouthArcIndex]);
	EXPECT_GT(velx, 0) << "post-fix: forward thrust drives east as it should";
	EXPECT_GT(velx, -vely) << "post-fix: eastward component dominates the small south sine";
}

// [rc4l] Structural invariant: after the fill, the ENTIRE quarter-circle tail mirrors the prefix.
// The pre-fix copy satisfied this for only the first half.
TEST(FineTable, WrapFillMirrorsWholeTail)
{
	auto t = BuildBaseSineTable();
	zx::FillFineSineWrap(t.data(), FINEANGLES);
	for (int m = 0; m < FINEANGLES / 4; m++)
		ASSERT_EQ(t[FINEANGLES + m], t[m]) << "tail element " << m << " not mirrored";
}

// [rc4l] finecosine matches cos across every fine angle -- in particular it is never spuriously
// zero. This is the property the whole circle must hold; the south arc was the only violation.
TEST(FineTable, CosineNonZeroAcrossSouthArc)
{
	auto t = BuildBaseSineTable();
	zx::FillFineSineWrap(t.data(), FINEANGLES);
	const double pimul = kPi * 2 / FINEANGLES;
	for (int k = FINEANGLES * 7 / 8; k < FINEANGLES; k++) // 315-360 deg
	{
		const int64_t expected = int64_t(FRACUNIT * std::cos(k * pimul));
		if (std::llabs(expected) > 2)
			EXPECT_NE(Cosine(t, k), 0) << "finecosine still zeroed at k=" << k;
	}
}

TEST(FineTable, WrapCountIsQuarterCircle)
{
	EXPECT_EQ(zx::ComputeFineSineWrapCount(FINEANGLES), FINEANGLES / 4);
	EXPECT_EQ(zx::ComputeFineSineWrapCount(4096), 1024);
}
