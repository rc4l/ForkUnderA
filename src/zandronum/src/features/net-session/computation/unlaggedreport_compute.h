// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] fua_unlagged_report's shots: on a server, where unlagged put every other player for a hitscan and where they really were; on a client, where its own shot found them, so a shot's two halves can be set side by side.

#ifndef ZX_UNLAGGEDREPORT_COMPUTE_H
#define ZX_UNLAGGEDREPORT_COMPUTE_H

#include <deque>
#include <string>
#include <vector>

namespace zx { namespace netsession {

// [rc4l] One other player at the shot: where the trace met them (rewound on a server) and where they were at the tic it ran in.
struct ShotPlayer
{
	int player = -1;
	double x = 0, y = 0, z = 0;
	double presentX = 0, presentY = 0, presentZ = 0;
};

// [rc4l] One hitscan by a player: its number since the last reset, the shooter, the tic it ran in, the tic unlagged rewound to (the tic itself on a client), the player it hit or -1, whether this side traced it (a client only with cl_clientsidepuffs) and the network id of the actor its trace hit or -1.
struct UnlaggedShot
{
	int number = 0;
	int shooter = -1;
	int tic = 0;
	int rewindTic = 0;
	int hitPlayer = -1;
	bool traced = false;
	int hitNetId = -1;
	std::vector<ShotPlayer> players;
};

// [rc4l] The shots kept, the most recent kShotsKept, and the number the next one takes.
struct ShotLog
{
	std::deque<UnlaggedShot> shots;
	int next = 1;
};

enum { kShotsKept = 128 };

// [rc4l] Numbers shot and adds it to the log, the oldest going once kShotsKept are kept.
void ComputeLogShot(ShotLog &log, UnlaggedShot shot);

// [rc4l] The latest shot as this side traced it, hitting the actor of network id hitNetId (-1 for none); nothing with no shot logged.
void ComputeNoteTrace(ShotLog &log, int hitNetId);

std::string ComputeShotJson(const UnlaggedShot &shot);

// [rc4l] The report fua_unlagged_report prints: the role and every shot kept.
std::string ComputeShotLogJson(const ShotLog &log, const char *role);

}} // namespace zx::netsession

#endif // ZX_UNLAGGEDREPORT_COMPUTE_H
