// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Where cl_netgraph's graphs, gridlines, plotted points and legends land, so the drawing in cl_statistics.cpp only draws.

#ifndef ZX_NETGRAPH_COMPUTE_H
#define ZX_NETGRAPH_COMPUTE_H

#include <cstddef>
#include <vector>

namespace zx { namespace netclient {

// [rc4l] Ported from zandronum@9a8ae9d825e8: a graph holds the last minute, one point a second, 8 pixels apart and 120 tall.
constexpr int NETGRAPH_SECONDS = 60;
constexpr int NETGRAPH_TICK_WIDTH = 8;
constexpr int NETGRAPH_WIDTH = NETGRAPH_TICK_WIDTH * ( NETGRAPH_SECONDS - 1 );
constexpr int NETGRAPH_HEIGHT = 120;
constexpr int NETGRAPH_LEGEND_LINE_WIDTH = 16;
constexpr int NETGRAPH_LEGEND_SPACING = 10;

// [rc4l] The largest of count values, 0 for none.
unsigned int ComputeLargestValue(const unsigned int *values, std::size_t count);

// [rc4l] The scale's top: the default, or each line's largest value plus the bonus when that is more, never 0.
unsigned int ComputeNetGraphScaleMax(unsigned int defaultScaleMax, unsigned int scaleBonus, const std::vector<unsigned int> &lineMaxes);

// [rc4l] The number printed at the scale's top, in the divider's units.
unsigned int ComputeNetGraphScaleTop(unsigned int scaleMax, unsigned int scaleDivider);

// [rc4l] Where the point for second (0 the oldest) lands across the graph.
int ComputeNetGraphPointX(int leftX, unsigned int second);

// [rc4l] Where a value lands up the graph, the bottom for 0 and the top at the scale's top.
int ComputeNetGraphPointY(unsigned int value, int bottomY, unsigned int scaleMax);

// [rc4l] The vertical gridlines, one every ten seconds, moving left as the seconds pass.
std::vector<int> ComputeNetGraphVerticalGridLines(int leftX, int secondsPassed);

// [rc4l] The horizontal gridlines, at each quarter of the height.
std::vector<int> ComputeNetGraphHorizontalGridLines(int topY);

// [rc4l] The top of graph index (0 the lowest) stacked above the stats' text at statTopY.
int ComputeNetGraphTopY(int statTopY, int graphIndex, int fontHeight);

// [rc4l] One line's legend: whether it has a name, and that name's width.
struct NetGraphLegendEntry
{
	bool named;
	int textWidth;
};

// [rc4l] Where a legend's line starts, its text following a spacing after the line, or drawn false when it is left out.
struct NetGraphLegendPlace
{
	bool drawn;
	int x;
	int y;
};

// [rc4l] Each legend placed left to right from the graph's top-left, wrapping to new rows and stopping where no row fits.
std::vector<NetGraphLegendPlace> ComputeNetGraphLegend(int leftX, int topY, const std::vector<NetGraphLegendEntry> &entries, int fontHeight);

}} // namespace zx::netclient

#endif // ZX_NETGRAPH_COMPUTE_H
