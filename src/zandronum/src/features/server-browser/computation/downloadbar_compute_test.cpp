// [Doruk-Aksoy] Tests for downloadbar_compute: the download fraction, where the bar sits, and its fill, sheen and bouncing block.
#include <gtest/gtest.h>

#include "features/server-browser/computation/downloadbar_compute.h"

using namespace zx;

namespace
{

TEST(DownloadBar, FractionIsReceivedOverTotalClamped)
{
	EXPECT_FLOAT_EQ(0.5f, ComputeDownloadFraction(50, 100));
	EXPECT_FLOAT_EQ(1.f, ComputeDownloadFraction(150, 100));
	EXPECT_FLOAT_EQ(0.f, ComputeDownloadFraction(-5, 100));
}

TEST(DownloadBar, AnUnknownSizeHasNoFraction)
{
	EXPECT_FLOAT_EQ(-1.f, ComputeDownloadFraction(10, -1));
	EXPECT_FLOAT_EQ(-1.f, ComputeDownloadFraction(10, 0));
}

TEST(DownloadBar, StripSitsUnderTheHeaderWhenOneShows)
{
	// [Doruk-Aksoy] Under the bar rather than over it, so the tabs a click still reaches are never hidden.
	const NoticeStrip s = ComputeNoticeStrip(48, 10, 20);
	EXPECT_EQ(48, s.top);
	EXPECT_EQ(20, s.height);
}

TEST(DownloadBar, StripIsThePlainBandWithoutAHeader)
{
	const NoticeStrip s = ComputeNoticeStrip(0, 10, 20);
	EXPECT_EQ(10, s.top);
	EXPECT_EQ(20, s.height);
	EXPECT_EQ(0, ComputeNoticeStrip(0, 10, -3).height);
}

TEST(DownloadBar, AKnownFractionFillsFromTheLeft)
{
	const DownloadBarFill f = ComputeDownloadBarFill(1000, 0.25f, 0);
	EXPECT_EQ(0, f.fillX);
	EXPECT_EQ(250, f.fillW);
	EXPECT_EQ(1000, ComputeDownloadBarFill(1000, 1.5f, 0).fillW);
}

TEST(DownloadBar, TheSheenSweepsInsideTheFill)
{
	// [Doruk-Aksoy] Width 1200 makes the sheen 100 wide; a half fill is 600, so it travels 700 over the period.
	const DownloadBarFill start = ComputeDownloadBarFill(1200, 0.5f, 0);
	EXPECT_EQ(0, start.sheenW);

	const DownloadBarFill mid = ComputeDownloadBarFill(1200, 0.5f, 800);
	EXPECT_EQ(250, mid.sheenX);
	EXPECT_EQ(100, mid.sheenW);

	const DownloadBarFill late = ComputeDownloadBarFill(1200, 0.5f, 1599);
	EXPECT_LE(late.sheenX + late.sheenW, 600);
	EXPECT_EQ(late.sheenX, ComputeDownloadBarFill(1200, 0.5f, 1599 + 1600).sheenX);
}

TEST(DownloadBar, AnEmptyFillHasNoSheen)
{
	const DownloadBarFill f = ComputeDownloadBarFill(1200, 0.f, 800);
	EXPECT_EQ(0, f.fillW);
	EXPECT_EQ(0, f.sheenW);
}

TEST(DownloadBar, AnUnknownSizeBouncesABlockEndToEnd)
{
	EXPECT_EQ(0, ComputeDownloadBarFill(1000, -1.f, 0).fillX);
	EXPECT_EQ(250, ComputeDownloadBarFill(1000, -1.f, 0).fillW);
	EXPECT_EQ(750, ComputeDownloadBarFill(1000, -1.f, 1000).fillX);
	EXPECT_EQ(375, ComputeDownloadBarFill(1000, -1.f, 1500).fillX);
	EXPECT_EQ(0, ComputeDownloadBarFill(1000, -1.f, 1000).sheenW);
}

TEST(DownloadBar, ANegativeWidthDrawsNothing)
{
	const DownloadBarFill f = ComputeDownloadBarFill(-5, 0.5f, 100);
	EXPECT_EQ(0, f.fillW);
	EXPECT_EQ(0, f.sheenW);
}

} // namespace
