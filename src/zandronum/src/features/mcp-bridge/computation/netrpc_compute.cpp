// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/mcp-bridge/computation/netrpc_compute.h"
#include "features/json/computation/json_compute.h"

#include <algorithm>
#include <stdio.h>

namespace zx { namespace mcp {

namespace {

std::string Num(double v)
{
	char buf[48];
	snprintf(buf, sizeof(buf), "%.3f", v);
	return buf;
}

std::string Quoted(const std::string &s)
{
	return zx::json::ComputeJsonQuoted(s);
}

}

const char *ComputeNetRoleName(int netState)
{
	static const char *const kNames[] = { "single", "skirmish", "client", "server" };
	return netState >= 0 && netState < 4 ? kNames[netState] : "unknown";
}

const char *ComputeConnectionStateName(int state)
{
	static const char *const kNames[] = { "disconnected", "connecting", "authenticating", "requesting_snapshot", "receiving_snapshot", "active" };
	return state >= 0 && state < 6 ? kNames[state] : "unknown";
}

const char *ComputeClientSlotStateName(int state)
{
	static const char *const kNames[] = { "free", "challenge", "connected", "authenticated_outdated_map", "authenticated", "spawned_needs_authentication", "spawned" };
	return state >= 0 && state < 7 ? kNames[state] : "unknown";
}

std::string ComputeNetClientJson(const NetClientRow &row)
{
	return "{\"player\":" + std::to_string(row.player) + ",\"name\":" + Quoted(row.name) + ",\"state\":" + Quoted(ComputeClientSlotStateName(row.state)) +
		",\"ping\":" + std::to_string(row.ping) + ",\"lastServerTic\":" + std::to_string(row.lastServerTic) + ",\"clientTic\":" + std::to_string(row.clientTic) +
		",\"spectating\":" + (row.spectating ? "true" : "false") + ",\"bot\":" + (row.bot ? "true" : "false") + "}";
}

std::string ComputeWorldActorJson(const WorldActorRow &row)
{
	return "{\"netid\":" + std::to_string(row.netId) + ",\"c\":" + Quoted(row.cls) + ",\"x\":" + Num(row.x) + ",\"y\":" + Num(row.y) + ",\"z\":" + Num(row.z) +
		",\"vx\":" + Num(row.velX) + ",\"vy\":" + Num(row.velY) + ",\"vz\":" + Num(row.velZ) + ",\"angle\":" + Num(row.angle) +
		",\"health\":" + std::to_string(row.health) + ",\"player\":" + std::to_string(row.player) + ",\"sprite\":" + Quoted(row.sprite) + "}";
}

void ComputeSortActorRows(std::vector<WorldActorRow> &rows)
{
	std::stable_sort(rows.begin(), rows.end(), [](const WorldActorRow &a, const WorldActorRow &b) { return a.netId < b.netId; });
}

std::string ComputeSectorStateJson(const SectorStateRow &row)
{
	return "{\"i\":" + std::to_string(row.index) + ",\"floor\":" + Num(row.floor) + ",\"ceiling\":" + Num(row.ceiling) + ",\"light\":" + std::to_string(row.light) +
		",\"color\":" + std::to_string(row.color & 0xffffffu) + ",\"fade\":" + std::to_string(row.fade & 0xffffffu) + ",\"desat\":" + std::to_string(row.desaturation) +
		",\"floorTex\":" + Quoted(row.floorTex) + ",\"ceilingTex\":" + Quoted(row.ceilingTex) + ",\"gravity\":" + Num(row.gravity) +
		",\"friction\":" + Num(row.friction) + ",\"special\":" + std::to_string(row.special) + ",\"reflect\":[" + Num(row.reflectFloor) + "," +
		Num(row.reflectCeiling) + "],\"scale\":[" + Num(row.xScaleFloor) + "," + Num(row.yScaleFloor) + "," + Num(row.xScaleCeiling) + "," +
		Num(row.yScaleCeiling) + "],\"rotation\":[" + Num(row.angleFloor) + "," + Num(row.angleCeiling) + "]}";
}

std::string ComputeSideStateJson(const SideStateRow &row)
{
	return "{\"i\":" + std::to_string(row.index) + ",\"top\":" + Quoted(row.top) + ",\"mid\":" + Quoted(row.mid) + ",\"bottom\":" + Quoted(row.bottom) +
		",\"light\":" + std::to_string(row.light) + ",\"flags\":" + std::to_string(row.flags) + "}";
}

std::string ComputeLineStateJson(const LineStateRow &row)
{
	return "{\"i\":" + std::to_string(row.index) + ",\"alpha\":" + Num(row.alpha) + ",\"flags\":" + std::to_string(row.flags) + "}";
}

void ComputePageRange(int total, long first, long count, int &begin, int &end)
{
	const long t = total > 0 ? total : 0;
	const long b = first < 0 ? 0 : (first > t ? t : first);
	const long e = count < 0 ? t : (b + count > t ? t : b + count);
	begin = (int)b;
	end = (int)e;
}

}} // namespace zx::mcp
