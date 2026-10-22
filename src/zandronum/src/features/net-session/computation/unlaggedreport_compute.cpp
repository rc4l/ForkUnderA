// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-session/computation/unlaggedreport_compute.h"

#include <stdio.h>

namespace zx { namespace netsession {

namespace {

std::string Num(double v)
{
	char buf[48];
	snprintf(buf, sizeof(buf), "%.3f", v);
	return buf;
}

}

void ComputeLogShot(ShotLog &log, UnlaggedShot shot)
{
	shot.number = log.next++;
	log.shots.push_back(shot);
	while (log.shots.size() > (size_t)kShotsKept) log.shots.pop_front();
}

std::string ComputeShotJson(const UnlaggedShot &shot)
{
	std::string s = "{\"n\":" + std::to_string(shot.number) + ",\"shooter\":" + std::to_string(shot.shooter) + ",\"tic\":" + std::to_string(shot.tic) +
		",\"rewind\":" + std::to_string(shot.rewindTic) + ",\"hit\":" + std::to_string(shot.hitPlayer) + ",\"players\":[";
	for (size_t i = 0; i < shot.players.size(); i++)
	{
		const ShotPlayer &p = shot.players[i];
		s += (i ? ",{\"p\":" : "{\"p\":") + std::to_string(p.player) + ",\"x\":" + Num(p.x) + ",\"y\":" + Num(p.y) + ",\"z\":" + Num(p.z) +
			",\"px\":" + Num(p.presentX) + ",\"py\":" + Num(p.presentY) + ",\"pz\":" + Num(p.presentZ) + "}";
	}
	return s + "]}";
}

std::string ComputeShotLogJson(const ShotLog &log, const char *role)
{
	std::string s = std::string("{\"role\":\"") + role + "\",\"shots\":[";
	for (size_t i = 0; i < log.shots.size(); i++) s += (i ? "," : "") + ComputeShotJson(log.shots[i]);
	return s + "]}";
}

}} // namespace zx::netsession
