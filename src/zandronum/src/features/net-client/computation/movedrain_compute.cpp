// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-client/computation/movedrain_compute.h"
#include "features/net-session/computation/movebuffer_compute.h"

namespace zx { namespace netclient {

unsigned ComputeConsistentArrivals(unsigned arrivals, int lastArrivalTick, int gametic)
{
	return lastArrivalTick == gametic ? arrivals + 1 : 0;
}

std::vector<int> ComputeMoveDrainOrder(const std::vector<MoveDrainPlayer> &players, unsigned ticRate)
{
	std::vector<int> order;
	for (int i = 0; i < static_cast<int>(players.size()); i++)
	{
		if (players[i].active && players[i].buffered > 0)
			order.push_back(i);
	}
	for (int i = 0; i < static_cast<int>(players.size()); i++)
	{
		// [rc4l] What the first pass left: one fewer than were buffered.
		const unsigned left = players[i].buffered > 0 ? players[i].buffered - 1 : 0;
		if (players[i].active && left > 0 && zx::net::ComputeRunsSecondMoveCmd(left, players[i].consistentArrivals, ticRate))
			order.push_back(i);
	}
	return order;
}

bool ComputeTickSavesPrevPosition(bool inClientMode, bool isPlayer, bool spectating, bool moveBufferOn)
{
	// [rc4l] Ported from zandronum@318c98576461451b96de517c589204cd3f326725: the drain skips spectators, so their tick must save it.
	return !inClientMode || !isPlayer || spectating || !moveBufferOn;
}

bool ComputeUsesCommandBuffers(bool playingDemo, bool recorderBuffered, bool bufferCvar)
{
	return playingDemo ? recorderBuffered : bufferCvar;
}

bool ComputeBuffersCommand(bool draining, bool usesBuffers)
{
	return !draining && usesBuffers;
}

}} // namespace zx::netclient
