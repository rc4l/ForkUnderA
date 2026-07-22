// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Regression tests for the MIN/MAX/clamp mixed-type overloads added for the 64-bit
// fixed_t widening. Same-type calls must still bind to the original single-type template
// (unchanged behaviour); mixed int/int64 calls must resolve to the common (wider) type. This
// guards against a future edit reintroducing the ambiguity or changing the result type.
#include "gtest/gtest.h"
#include "templates.h"
#include <cstdint>
#include <type_traits>

namespace
{
TEST(Templates, SameTypeUnchanged)
{
	EXPECT_EQ(MIN(3, 7), 3);
	EXPECT_EQ(MAX(3, 7), 7);
	EXPECT_EQ(clamp(5, 0, 10), 5);
	EXPECT_EQ(clamp(-1, 0, 10), 0);
	EXPECT_EQ(clamp(11, 0, 10), 10);
	// Same-type result stays that type.
	static_assert(std::is_same<decltype(MAX(1, 2)), int>::value, "MAX(int,int) stays int");
}

TEST(Templates, MixedIntAndInt64ResolveToCommonType)
{
	const int64_t big = 1LL << 40;
	// These are the calls the flip broke: an int literal against a 64-bit value.
	EXPECT_EQ(MAX(1, big), big);
	EXPECT_EQ(MIN(1, big), (int64_t)1);
	EXPECT_EQ(MAX(big, 1), big);
	EXPECT_EQ(clamp((int)5, (int64_t)0, big), (int64_t)5);
	EXPECT_EQ(clamp(big, (int64_t)0, (int64_t)255), (int64_t)255);   // clamps a giant value down
	// Result of a mixed call is the wider (64-bit) type, so it is not truncated.
	static_assert(std::is_same<decltype(MAX(1, big)), int64_t>::value, "mixed MAX widens to int64");
	static_assert(std::is_same<decltype(clamp(1, (int64_t)0, big)), int64_t>::value, "mixed clamp widens");
}

TEST(Templates, MixedClampActuallyClampsBothEnds)
{
	const int64_t lo = -(1LL << 40), hi = 1LL << 40;
	EXPECT_EQ(clamp((int64_t)0, lo, hi), (int64_t)0);
	EXPECT_EQ(clamp(lo - 1, lo, hi), lo);
	EXPECT_EQ(clamp(hi + 1, lo, hi), hi);
}

// [rc4l] A convertible-but-non-copyable object (mimicking an FIntCVar: has operator int, no
// copy) must NOT bind to the mixed-type overloads -- those are arithmetic-only, so an explicit
// clamp<int>/MAX/MIN converts the object to int via the single-type template instead of trying
// to copy it. Guards the is_arithmetic constraint that fixed the sv_maxacsbanduration break.
namespace
{
struct IntLike
{
	int v;
	explicit IntLike(int x) : v(x) {}
	IntLike(const IntLike &) = delete;            // like FIntCVar: not copyable
	IntLike &operator=(const IntLike &) = delete;
	operator int() const { return v; }
};
}

TEST(Templates, NonArithmeticConvertibleUsesSingleTypeTemplate)
{
	IntLike hi(255);
	// If the mixed overload matched, it would try to copy IntLike (deleted) and fail to
	// compile. Binding to clamp<int> converts hi to int, so this compiles and clamps.
	EXPECT_EQ(clamp<int>(1000, 1, hi), 255);
	EXPECT_EQ(clamp<int>(-5, 1, hi), 1);
	EXPECT_EQ(MAX<int>(5, hi), 255);
	EXPECT_EQ(MIN<int>(5, hi), 5);
}
} // namespace
