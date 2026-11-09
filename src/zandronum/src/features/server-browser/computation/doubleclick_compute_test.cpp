// [Doruk-Aksoy] Tests for doubleclick_compute: two quick clicks on one target are a double, anything else is a single.
#include <gtest/gtest.h>

#include "features/server-browser/computation/doubleclick_compute.h"

using zx::ClickClock;
using zx::ComputeDoubleClick;

namespace
{

const int kWindow = 20;

TEST(DoubleClick, FirstClickIsSingle)
{
	ClickClock clock;
	EXPECT_FALSE(ComputeDoubleClick(clock, 3, 0, kWindow));
	EXPECT_EQ(clock.target, 3);
	EXPECT_EQ(clock.time, 0);
}

TEST(DoubleClick, SecondClickOnSameTargetInsideWindowIsDouble)
{
	ClickClock clock;
	ComputeDoubleClick(clock, 3, 100, kWindow);
	EXPECT_TRUE(ComputeDoubleClick(clock, 3, 100 + kWindow, kWindow));
}

TEST(DoubleClick, SecondClickPastWindowIsSingle)
{
	ClickClock clock;
	ComputeDoubleClick(clock, 3, 100, kWindow);
	EXPECT_FALSE(ComputeDoubleClick(clock, 3, 100 + kWindow + 1, kWindow));
	EXPECT_EQ(clock.time, 100 + kWindow + 1);
}

TEST(DoubleClick, SecondClickOnOtherTargetIsSingleAndRearms)
{
	ClickClock clock;
	ComputeDoubleClick(clock, 3, 100, kWindow);
	EXPECT_FALSE(ComputeDoubleClick(clock, 4, 105, kWindow));
	EXPECT_TRUE(ComputeDoubleClick(clock, 4, 110, kWindow));
}

TEST(DoubleClick, ThirdClickAfterDoubleIsNotAnotherDouble)
{
	ClickClock clock;
	ComputeDoubleClick(clock, 3, 100, kWindow);
	ASSERT_TRUE(ComputeDoubleClick(clock, 3, 105, kWindow));
	EXPECT_FALSE(ComputeDoubleClick(clock, 3, 110, kWindow));
	EXPECT_TRUE(ComputeDoubleClick(clock, 3, 112, kWindow));
}

TEST(DoubleClick, NegativeTargetNeverDoubles)
{
	ClickClock clock;
	ComputeDoubleClick(clock, -1, 100, kWindow);
	EXPECT_FALSE(ComputeDoubleClick(clock, -1, 101, kWindow));
}

TEST(DoubleClick, FreshClockAtTimeZeroDoesNotDoubleOnUnsetTarget)
{
	ClickClock clock;
	EXPECT_FALSE(ComputeDoubleClick(clock, -1, -1000, kWindow));
}

} // namespace
