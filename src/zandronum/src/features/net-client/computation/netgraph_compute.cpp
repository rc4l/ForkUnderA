// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-client/computation/netgraph_compute.h"

#include <cstdint>

namespace zx { namespace netclient {

unsigned int ComputeLargestValue(const unsigned int *values, std::size_t count)
{
	unsigned int largest = 0;
	for (std::size_t i = 0; i < count; i++)
	{
		if (values[i] > largest)
			largest = values[i];
	}
	return largest;
}

unsigned int ComputeNetGraphScaleMax(unsigned int defaultScaleMax, unsigned int scaleBonus, const std::vector<unsigned int> &lineMaxes)
{
	unsigned int scaleMax = defaultScaleMax;
	for (unsigned int lineMax : lineMaxes)
	{
		if (lineMax + scaleBonus > scaleMax)
			scaleMax = lineMax + scaleBonus;
	}
	return scaleMax > 0 ? scaleMax : 1;
}

unsigned int ComputeNetGraphScaleTop(unsigned int scaleMax, unsigned int scaleDivider)
{
	return scaleDivider > 1 ? scaleMax / scaleDivider : scaleMax;
}

int ComputeNetGraphPointX(int leftX, unsigned int second)
{
	return leftX + NETGRAPH_TICK_WIDTH * static_cast<int>(second);
}

int ComputeNetGraphPointY(unsigned int value, int bottomY, unsigned int scaleMax)
{
	// [rc4l] Widened before the multiply, where an unsigned product wrapped past 35 million bytes a second.
	const std::uint64_t scaled = (static_cast<std::uint64_t>(value) * NETGRAPH_HEIGHT) / (scaleMax > 0 ? scaleMax : 1);
	return bottomY - static_cast<int>(scaled < NETGRAPH_HEIGHT ? scaled : NETGRAPH_HEIGHT);
}

std::vector<int> ComputeNetGraphVerticalGridLines(int leftX, int secondsPassed)
{
	constexpr int SECONDS_BETWEEN = NETGRAPH_SECONDS / 6;
	const int seconds = secondsPassed >= 0 ? secondsPassed : 0;
	std::vector<int> lines;
	for (int x = leftX + (SECONDS_BETWEEN - seconds % SECONDS_BETWEEN) * NETGRAPH_TICK_WIDTH; x < leftX + NETGRAPH_WIDTH; x += SECONDS_BETWEEN * NETGRAPH_TICK_WIDTH)
		lines.push_back(x);
	return lines;
}

std::vector<int> ComputeNetGraphHorizontalGridLines(int topY)
{
	std::vector<int> lines;
	for (int y = topY + NETGRAPH_HEIGHT / 4; y < topY + NETGRAPH_HEIGHT; y += NETGRAPH_HEIGHT / 4)
		lines.push_back(y);
	return lines;
}

int ComputeNetGraphTopY(int statTopY, int graphIndex, int fontHeight)
{
	// [rc4l] The lowest sits four pixels above the text, and each next one eight pixels and its label's height above that.
	return statTopY - (NETGRAPH_HEIGHT + 4) - graphIndex * (NETGRAPH_HEIGHT + 8 + fontHeight);
}

std::vector<NetGraphLegendPlace> ComputeNetGraphLegend(int leftX, int topY, const std::vector<NetGraphLegendEntry> &entries, int fontHeight)
{
	const int rightX = leftX + NETGRAPH_WIDTH;
	const int bottomY = topY + NETGRAPH_HEIGHT;
	std::vector<NetGraphLegendPlace> places(entries.size(), NetGraphLegendPlace{ false, 0, 0 });
	int x = leftX + NETGRAPH_LEGEND_SPACING;
	int y = topY + NETGRAPH_LEGEND_SPACING;

	for (std::size_t i = 0; i < entries.size(); i++)
	{
		if (entries[i].named == false)
			continue;

		const int total = entries[i].textWidth + NETGRAPH_LEGEND_LINE_WIDTH + NETGRAPH_LEGEND_SPACING;
		if (x + total > rightX - NETGRAPH_LEGEND_SPACING)
		{
			// [rc4l] A legend wider than the graph is left out, and the rest stop once no row is left.
			if (total + 2 * NETGRAPH_LEGEND_SPACING > NETGRAPH_WIDTH)
				continue;

			x = leftX + NETGRAPH_LEGEND_SPACING;
			y += fontHeight + 2;
			if (y > bottomY - NETGRAPH_LEGEND_SPACING)
				break;
		}

		places[i] = NetGraphLegendPlace{ true, x, y };
		x += NETGRAPH_LEGEND_LINE_WIDTH + NETGRAPH_LEGEND_SPACING + entries[i].textWidth + NETGRAPH_LEGEND_SPACING;
	}
	return places;
}

}} // namespace zx::netclient
