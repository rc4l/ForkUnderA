// [rc4l] Coverage-guided harnesses for the 64-bit scale operations.
//
// Each MulScale64/DivScale64/DMulScale64/TMulScale64 has TWO implementations inside it: an
// int32 fast path that skips the 128-bit math, and the wide path for giant-map operands. A
// normal map takes the fast path and a giant map takes the wide one, so any disagreement
// between them is a bug that only shows up on certain maps -- precisely the failure mode that
// a hand-written value list is worst at finding and a coverage-guided search is best at, since
// the Fits32() boundary is a branch the fuzzer will steer straight at.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

#include <cstdint>

#include "features/fixed64/computation/fixed64_scale_compute.h"
#include "features/fixed64/computation/wide128_compute.h"

namespace
{

constexpr int FRACBITS = 16;
constexpr int64_t FRACUNIT = int64_t(1) << FRACBITS;

auto AnyShift() { return fuzztest::InRange<unsigned>(0, 63); }
auto MulAddOperand() { return fuzztest::InRange<int64_t>(-(int64_t(1) << 62), int64_t(1) << 62); }
auto NonZero() { return fuzztest::Filter([](int64_t v) { return v != 0; }, fuzztest::Arbitrary<int64_t>()); }

// [rc4l] A 48.16 fixed_t holds ~2^47 map units; that is the range a real coordinate lives in.
auto AnyCoordinate() { return fuzztest::InRange<int64_t>(-(int64_t(1) << 47), int64_t(1) << 47); }

// --- The int32 fast path must never disagree with the 128-bit path it skips. ---------------

void MulScaleFastPathMatchesWidePath(int64_t a, int64_t b, unsigned shift)
{
	ASSERT_EQ(zx::MulScale64(a, b, shift), zx::ComputeMulShiftS64(a, b, shift));
}
FUZZ_TEST(Fixed64ScaleFuzz, MulScaleFastPathMatchesWidePath)
	.WithDomains(fuzztest::Arbitrary<int64_t>(), fuzztest::Arbitrary<int64_t>(), AnyShift());

void DivScaleFastPathMatchesWidePath(int64_t a, unsigned shift, int64_t b)
{
	ASSERT_EQ(zx::DivScale64(a, shift, b), zx::ComputeDivShiftS64(a, shift, b));
}
FUZZ_TEST(Fixed64ScaleFuzz, DivScaleFastPathMatchesWidePath)
	.WithDomains(fuzztest::Arbitrary<int64_t>(), AnyShift(), NonZero());

void DMulScaleFastPathMatchesWidePath(int64_t a, int64_t b, int64_t c, int64_t d, unsigned shift)
{
	ASSERT_EQ(zx::DMulScale64(a, b, c, d, shift), zx::ComputeMulAddShiftS64(a, b, c, d, shift));
}
FUZZ_TEST(Fixed64ScaleFuzz, DMulScaleFastPathMatchesWidePath)
	.WithDomains(MulAddOperand(), MulAddOperand(), MulAddOperand(), MulAddOperand(), AnyShift());

void TMulScaleFastPathMatchesWidePath(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e,
									  int64_t f, unsigned shift)
{
	ASSERT_EQ(zx::TMulScale64(a, b, c, d, e, f, shift),
			  zx::ComputeMulAdd3ShiftS64(a, b, c, d, e, f, shift));
}
FUZZ_TEST(Fixed64ScaleFuzz, TMulScaleFastPathMatchesWidePath)
	.WithDomains(MulAddOperand(), MulAddOperand(), MulAddOperand(), MulAddOperand(),
				 MulAddOperand(), MulAddOperand(), AnyShift());

// --- Fixed-point identities that must hold across both paths. ------------------------------

// [rc4l] Multiplying a coordinate by 1.0 is the identity; the operand range deliberately
// straddles Fits32() so the fuzzer covers both the fast and the wide path with one property.
void FixedMulByOneIsIdentity(int64_t v)
{
	ASSERT_EQ(zx::Fixed64Mul(v, FRACUNIT), v);
}
FUZZ_TEST(Fixed64ScaleFuzz, FixedMulByOneIsIdentity).WithDomains(AnyCoordinate());

void FixedDivByOneIsIdentity(int64_t v)
{
	ASSERT_EQ(zx::Fixed64Div(v, FRACUNIT), v);
}
FUZZ_TEST(Fixed64ScaleFuzz, FixedDivByOneIsIdentity).WithDomains(AnyCoordinate());

// --- AlignDownPow2: the polyobject-rotation mask that lost its sign under the widening. -----

// [rc4l] Stated as invariants rather than against a reference, because the bug it guards
// (0xFFFFFE00 zero-extending and wiping the sign) is exactly a reference-implementation
// mistake. bits stops at 62 so 1<<bits stays inside int64_t.
void AlignDownPow2LandsOnAMultipleAtOrBelowV(int64_t v, unsigned bits)
{
	const int64_t aligned = zx::AlignDownPow2(v, bits);
	const int64_t step = int64_t(1) << bits;
	ASSERT_LE(aligned, v) << "aligned upward";
	ASSERT_EQ(aligned & (step - 1), 0) << "not a multiple of 2^bits";
	ASSERT_LT(v - aligned, step) << "aligned down by more than one step";
}
FUZZ_TEST(Fixed64ScaleFuzz, AlignDownPow2LandsOnAMultipleAtOrBelowV)
	.WithDomains(fuzztest::Arbitrary<int64_t>(), fuzztest::InRange<unsigned>(0, 62));

// --- Mul32Wrap: the deliberately-emulated 32-bit overflow (BCOMPATF_SETSLOPEOVERFLOW). ------

// [rc4l] It must keep wrapping at 2^32 after the widening, i.e. stay the low 32 bits of the
// product rather than becoming the full 64-bit result.
void Mul32WrapKeepsLow32BitsOfProduct(int32_t a, int32_t b)
{
	const int64_t full = int64_t(a) * b;
	ASSERT_EQ(zx::Mul32Wrap(a, b), int32_t(uint32_t(uint64_t(full))));
}
FUZZ_TEST(Fixed64ScaleFuzz, Mul32WrapKeepsLow32BitsOfProduct);

} // namespace
