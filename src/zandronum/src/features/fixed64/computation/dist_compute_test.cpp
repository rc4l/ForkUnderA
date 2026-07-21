// [rc4l] Regression tests for the "distance truncated to int before it's reduced" handoff bugs
// (point pusher, seeker missile). Each proves that a far-but-legal distance -- whose fixed value
// exceeds INT32_MAX -- now reduces correctly, and that the pre-fix int truncation produced the
// wrong result.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gtest/gtest.h"

#include <cstdint>

#include "features/fixed64/computation/dist_compute.h"

namespace
{
constexpr int64_t U(int64_t units) { return units << 16; } // map units -> 16.16 fixed
} // namespace

// [rc4l] A pusher acting across a ~46k-unit span: the fixed distance exceeds INT32_MAX. At full
// width the magnitude has fallen off, so speed <= 0 (outside the effective radius). The pre-fix
// `int dist` truncated the distance, flipping its sign and spuriously pushing.
TEST(DistCompute, PusherFarDistanceGivesNoPush)
{
	const int64_t farDist = U(46000); // fixed value 3,014,656,000 > INT32_MAX
	EXPECT_LE(zx::ComputePusherSpeed(farDist, 10, 7, 16), 0);

	// Pre-fix behaviour: `int dist = P_AproxDistance(...)` truncated to 32 bits.
	const int32_t truncDist = static_cast<int32_t>(farDist);
	const int32_t brokenInner = 10 - ((truncDist >> 16) >> 1);
	const int brokenSpeed = static_cast<int32_t>(static_cast<uint32_t>(brokenInner) << 8);
	EXPECT_GT(brokenSpeed, 0) << "pre-fix truncation spuriously pushed a far-away thing";
}

// [rc4l] Close range still pushes, and matches the plain formula exactly (no behavior change).
// At 100 units: (100>>16>>1 == 50), magnitude 100 - 50 = 50, << (16-7-1)=8 -> 12800.
TEST(DistCompute, PusherNearDistancePushes)
{
	EXPECT_EQ(zx::ComputePusherSpeed(U(100), 100, 7, 16), static_cast<int32_t>(50u << 8));
	EXPECT_GT(zx::ComputePusherSpeed(U(100), 100, 7, 16), 0);
}

// [rc4l] Seeker tracking a far target: full-width dist/speed yields the right tic count and a
// gentle velz. The pre-fix truncation made tics collapse to 1 and slammed velz to the whole gap.
TEST(DistCompute, SeekerFarTargetVelZ)
{
	const int64_t dist = U(46000); // > INT32_MAX in fixed
	const int64_t speed = U(20);
	const int64_t zdiff = U(1000);

	const int64_t velz = zx::ComputeSeekerVelZ(dist, speed, zdiff);
	EXPECT_EQ(velz, zdiff / 2300); // tics = 46000/20 = 2300
	EXPECT_LT(velz, zdiff) << "velz must be a fraction of the gap";

	// Pre-fix: int dist truncates negative -> tics clamps to 1 -> velz == whole gap.
	const int32_t truncDist = static_cast<int32_t>(dist);
	int64_t brokenTics = static_cast<int64_t>(truncDist) / speed;
	if (brokenTics < 1)
		brokenTics = 1;
	EXPECT_EQ(zdiff / brokenTics, zdiff) << "pre-fix slammed velz to the whole gap";
}

// [rc4l] Near target unaffected: tics = 400/20 = 20.
TEST(DistCompute, SeekerNearTargetVelZ)
{
	EXPECT_EQ(zx::ComputeSeekerVelZ(U(400), U(20), U(1000)), U(1000) / 20);
}
