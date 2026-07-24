// [rc4l] Coverage-guided differential harnesses for the 128-bit primitives.
//
// wide128_compute_test.cpp already sweeps these with a fixed-seed LCG over a hand-chosen
// +/-2^40 window. These harnesses state the same properties over the FULL operand range and
// let the fuzzer steer toward uncovered branches, which matters most for the *Soft routines:
// they are the ARM64-MSVC path, so on every compiler we actually build with, the only thing
// exercising them is this differential against the native __int128 result.
//
// Every reference below forms its 128-bit intermediate by multiplication rather than a left
// shift -- shifting a negative signed value is UB, the exact trap fixed in fa93b7f.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

#include <cstdint>

#include "features/fixed64/computation/wide128_compute.h"

namespace
{

// [rc4l] Every public entry point documents "shift in [0,63]"; that is a precondition on the
// caller, so the domain enforces it rather than reporting a shift-by-64 the engine can't reach.
auto AnyShift() { return fuzztest::InRange<unsigned>(0, 63); }

// [rc4l] The multiply-add forms sum two or three 128-bit products, so operands are capped at
// 2^62 to keep the sum inside __int128 -- past that the reference itself would overflow.
auto MulAddOperand() { return fuzztest::InRange<int64_t>(-(int64_t(1) << 62), int64_t(1) << 62); }

// [rc4l] "Caller guarantees b != 0" for every divide.
auto NonZero() { return fuzztest::Filter([](int64_t v) { return v != 0; }, fuzztest::Arbitrary<int64_t>()); }
auto NonZeroU() { return fuzztest::Filter([](uint64_t v) { return v != 0; }, fuzztest::Arbitrary<uint64_t>()); }

// [rc4l] v << s built as a multiply so a negative v stays defined.
__int128 Shl128(__int128 v, unsigned s) { return v * (__int128(1) << s); }

// --- The software path must be bit-identical to the native path it replaces. ---------------

void MulShiftSoftMatchesNative(int64_t a, int64_t b, unsigned shift)
{
	ASSERT_EQ(zx::ComputeMulShiftS64Soft(a, b, shift), zx::ComputeMulShiftS64(a, b, shift));
}
FUZZ_TEST(Wide128Fuzz, MulShiftSoftMatchesNative)
	.WithDomains(fuzztest::Arbitrary<int64_t>(), fuzztest::Arbitrary<int64_t>(), AnyShift());

void DivShiftSoftMatchesNative(int64_t a, unsigned shift, int64_t b)
{
	ASSERT_EQ(zx::ComputeDivShiftS64Soft(a, shift, b), zx::ComputeDivShiftS64(a, shift, b));
}
FUZZ_TEST(Wide128Fuzz, DivShiftSoftMatchesNative)
	.WithDomains(fuzztest::Arbitrary<int64_t>(), AnyShift(), NonZero());

void MulAddShiftSoftMatchesNative(int64_t a, int64_t b, int64_t c, int64_t d, unsigned shift)
{
	ASSERT_EQ(zx::ComputeMulAddShiftS64Soft(a, b, c, d, shift),
			  zx::ComputeMulAddShiftS64(a, b, c, d, shift));
}
FUZZ_TEST(Wide128Fuzz, MulAddShiftSoftMatchesNative)
	.WithDomains(MulAddOperand(), MulAddOperand(), MulAddOperand(), MulAddOperand(), AnyShift());

void MulAdd3ShiftSoftMatchesNative(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f,
								   unsigned shift)
{
	ASSERT_EQ(zx::ComputeMulAdd3ShiftS64Soft(a, b, c, d, e, f, shift),
			  zx::ComputeMulAdd3ShiftS64(a, b, c, d, e, f, shift));
}
FUZZ_TEST(Wide128Fuzz, MulAdd3ShiftSoftMatchesNative)
	.WithDomains(MulAddOperand(), MulAddOperand(), MulAddOperand(), MulAddOperand(),
				 MulAddOperand(), MulAddOperand(), AnyShift());

void MulDivSoftMatchesNative(int64_t a, int64_t b, int64_t c)
{
	ASSERT_EQ(zx::ComputeMulDivS64Soft(a, b, c), zx::ComputeMulDivS64(a, b, c));
}
FUZZ_TEST(Wide128Fuzz, MulDivSoftMatchesNative)
	.WithDomains(fuzztest::Arbitrary<int64_t>(), fuzztest::Arbitrary<int64_t>(), NonZero());

// --- And both paths must match a true 128-bit computation. ---------------------------------

void MulShiftMatchesWideReference(int64_t a, int64_t b, unsigned shift)
{
	const int64_t ref = int64_t((__int128(a) * b) >> shift);
	ASSERT_EQ(zx::ComputeMulShiftS64(a, b, shift), ref);
}
FUZZ_TEST(Wide128Fuzz, MulShiftMatchesWideReference)
	.WithDomains(fuzztest::Arbitrary<int64_t>(), fuzztest::Arbitrary<int64_t>(), AnyShift());

void DivShiftMatchesWideReference(int64_t a, unsigned shift, int64_t b)
{
	const int64_t ref = int64_t(Shl128(a, shift) / b);
	ASSERT_EQ(zx::ComputeDivShiftS64(a, shift, b), ref);
}
FUZZ_TEST(Wide128Fuzz, DivShiftMatchesWideReference)
	.WithDomains(fuzztest::Arbitrary<int64_t>(), AnyShift(), NonZero());

void MulDivMatchesWideReference(int64_t a, int64_t b, int64_t c)
{
	const int64_t ref = int64_t((__int128(a) * b) / c);
	ASSERT_EQ(zx::ComputeMulDivS64(a, b, c), ref);
}
FUZZ_TEST(Wide128Fuzz, MulDivMatchesWideReference)
	.WithDomains(fuzztest::Arbitrary<int64_t>(), fuzztest::Arbitrary<int64_t>(), NonZero());

// --- The unsigned 128-bit building blocks. -------------------------------------------------

void UMul128SoftMatchesWideReference(uint64_t a, uint64_t b)
{
	const unsigned __int128 ref = (unsigned __int128)(a) * b;
	uint64_t hi = 0;
	const uint64_t lo = zx::ComputeUMul128Soft(a, b, &hi);
	ASSERT_EQ(lo, uint64_t(ref));
	ASSERT_EQ(hi, uint64_t(ref >> 64));
}
FUZZ_TEST(Wide128Fuzz, UMul128SoftMatchesWideReference);

void UDiv128SoftMatchesWideReference(uint64_t hi, uint64_t lo, uint64_t d)
{
	const unsigned __int128 num = ((unsigned __int128)(hi) << 64) | lo;
	ASSERT_EQ(zx::ComputeUDiv128Soft(hi, lo, d), uint64_t(num / d));
}
FUZZ_TEST(Wide128Fuzz, UDiv128SoftMatchesWideReference)
	.WithDomains(fuzztest::Arbitrary<uint64_t>(), fuzztest::Arbitrary<uint64_t>(), NonZeroU());

} // namespace
