// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The bridge's netgame reads as JSON: net.state (this instance's side of a netgame) and world.actors (the synced actors by network id), so two instances' worlds can be compared.

#ifndef ZX_NETRPC_COMPUTE_H
#define ZX_NETRPC_COMPUTE_H

#include <string>
#include <vector>

namespace zx { namespace mcp {

// [rc4l] NETWORK_GetState()'s role by name: single, skirmish (single-player multiplayer), client, server.
const char *ComputeNetRoleName(int netState);

// [rc4l] A client's CONNECTIONSTATE_e by name, from disconnected to active.
const char *ComputeConnectionStateName(int state);

// [rc4l] A server's CLIENTSTATE_e for one slot by name, from free to spawned.
const char *ComputeClientSlotStateName(int state);

// [rc4l] One client as the server holds it: its player, name, slot state, ping, the last server tic it said it saw and its own tic.
struct NetClientRow
{
	int player = -1;
	std::string name;
	int state = 0;
	int ping = 0;
	long long lastServerTic = 0;
	long long clientTic = 0;
	bool spectating = false;
	bool bot = false;
	std::string account;   // [rc4l] the account it proved at join (features/identity), empty before
};

std::string ComputeNetClientJson(const NetClientRow &row);

// [rc4l] One actor as this instance holds it, positions in map units and the angle in degrees.
struct WorldActorRow
{
	unsigned netId = 0;
	std::string cls;
	double x = 0, y = 0, z = 0;
	double velX = 0, velY = 0, velZ = 0;
	double angle = 0;
	int health = 0;
	int player = -1;     // [rc4l] the player whose pawn it is, or -1
	std::string sprite;  // [rc4l] the sprite and frame it shows, as "TROOA"
};

std::string ComputeWorldActorJson(const WorldActorRow &row);

// [rc4l] Rows in network id order, so two instances list the same actors in the same order whatever their thinker lists hold.
void ComputeSortActorRows(std::vector<WorldActorRow> &rows);

// [rc4l] One sector's look as a netgame syncs it: plane heights at its sound origin, light, colour and fade, and its flats' names.
struct SectorStateRow
{
	int index = 0;
	double floor = 0, ceiling = 0;
	int light = 0;
	unsigned color = 0, fade = 0;   // [rc4l] PalEntry values, rgb only
	int desaturation = 0;
	std::string floorTex, ceilingTex;
	// [rc4l] What else only a server writes of it (features/net-replication's families).
	double gravity = 0, friction = 0;
	int special = 0;
	double reflectFloor = 0, reflectCeiling = 0;
	double xScaleFloor = 0, yScaleFloor = 0, xScaleCeiling = 0, yScaleCeiling = 0;
	double angleFloor = 0, angleCeiling = 0;   // [rc4l] degrees
};

std::string ComputeSectorStateJson(const SectorStateRow &row);

// [rc4l] One sidedef's look: its three textures' names, its own light and its flags.
struct SideStateRow
{
	int index = 0;
	std::string top, mid, bottom;
	int light = 0;
	unsigned flags = 0;
};

std::string ComputeSideStateJson(const SideStateRow &row);

// [rc4l] One line's server-written look: its translucency and the flags clients move and draw by.
struct LineStateRow
{
	int index = 0;
	double alpha = 0;
	unsigned flags = 0;
};

std::string ComputeLineStateJson(const LineStateRow &row);

// [rc4l] The page [first, first + count) of total rows a paged read covers, clamped to what exists.
void ComputePageRange(int total, long first, long count, int &begin, int &end);

}} // namespace zx::mcp

#endif // ZX_NETRPC_COMPUTE_H
