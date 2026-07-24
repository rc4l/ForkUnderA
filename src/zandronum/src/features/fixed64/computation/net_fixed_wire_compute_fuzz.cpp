// [rc4l] Coverage-guided harnesses for the 32-bit network wire model.
//
// net_fixed_wire_compute_test.cpp pins the behaviour with 14 hand-picked values plus an
// 8192-angle sweep. These state the same contract as properties over the whole domain, and add
// the case the value list has no entry for: what a value ABOVE the wire's 32-bit ceiling does.
// The header documents that ceiling in prose but nothing asserted it, so a change to the
// truncation would have gone unnoticed.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

#include <cstdint>

#include "features/fixed64/computation/net_fixed_wire_compute.h"

namespace
{

constexpr int64_t FRACUNIT = int64_t(1) << zx::ZX_WIRE_FRACBITS;

// [rc4l] The "short" wire field carries the integer part in a signed 16-bit word, so it is
// faithful only while the map-unit coordinate fits a short.
constexpr int64_t kShortMin = int64_t(-32768) * FRACUNIT;
constexpr int64_t kShortMax = int64_t(32767) * FRACUNIT + (FRACUNIT - 1);

// --- The "long" field is exact for anything the wire can represent. ------------------------

void LongIsIdentityWithinWireRange(int64_t v)
{
	ASSERT_EQ(zx::WireRoundtripLong(v), v);
}
FUZZ_TEST(NetFixedWireFuzz, LongIsIdentityWithinWireRange)
	.WithDomains(fuzztest::InRange<int64_t>(INT32_MIN, INT32_MAX));

// [rc4l] Above the ceiling it must truncate to the low 32 bits and sign-extend back -- the
// documented, intentional limit. Asserting it means a silent change to that behaviour fails.
void LongTruncatesToLow32BitsOutsideWireRange(int64_t v)
{
	ASSERT_EQ(zx::WireRoundtripLong(v), int64_t(int32_t(v)));
}
FUZZ_TEST(NetFixedWireFuzz, LongTruncatesToLow32BitsOutsideWireRange);

// --- The "short" field keeps the integer part, sign included, and drops the fraction. ------

void ShortKeepsIntegerPartWithSign(int64_t v)
{
	// [rc4l] Arithmetic shift, so a negative coordinate rounds toward -inf exactly as the engine does.
	ASSERT_EQ(zx::WireRoundtripShort(v), (v >> zx::ZX_WIRE_FRACBITS) * FRACUNIT);
}
FUZZ_TEST(NetFixedWireFuzz, ShortKeepsIntegerPartWithSign)
	.WithDomains(fuzztest::InRange<int64_t>(kShortMin, kShortMax));

// [rc4l] The Shape-3 regression this whole unit exists to catch: a negative velocity or
// coordinate zero-extending into a huge positive as it crosses the wire.
void ShortNeverTurnsNegativeIntoPositive(int64_t v)
{
	ASSERT_LE(zx::WireRoundtripShort(v), 0) << "negative value came back positive off the wire";
}
FUZZ_TEST(NetFixedWireFuzz, ShortNeverTurnsNegativeIntoPositive)
	.WithDomains(fuzztest::InRange<int64_t>(kShortMin, -1));

} // namespace
