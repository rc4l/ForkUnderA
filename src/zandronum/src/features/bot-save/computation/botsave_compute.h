// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The bots in a savegame, as bytes.
//
// Bots ARE already written into a save: they occupy real player slots, so P_SerializePlayers stores
// their name, inventory, frags and their actor like anybody else's. They vanish on LOAD, in one
// line -- `playerUsed[i] = playeringame[i] ? 0 : 2` -- which will only match a saved player into a
// slot that is ALREADY occupied. Load into a fresh session and only the human's slot is, so the
// bots have nowhere to land and are dropped with their data unread.
//
// So this chunk is not a second copy of the bots. It is the roster that lets the loader re-occupy
// those slots before the matcher runs, after which the engine's own machinery restores everything
// it already saved.
//
// A CHUNK, NOT A CHANGE TO THE SAVE STREAM. Zandronum already does exactly this for skirmishes:
// mpEm is one byte recording NETSTATE_SINGLE_MULTIPLAYER, appended beside the image. An absent
// chunk means no bots, so every save ever written still loads, no SAVEVER bump is needed, and an
// older engine ignores it. It also keeps the diff out of upstream's ReadMultiplePlayers, which is
// shared by every load path in the engine.
//
// WHAT IS CARRIED, AND WHY SO LITTLE. Slot, name and team are what the matcher needs. Beyond that
// only "tier one" of CSkullBot: plain integers and bools describing how the bot was moving and
// aiming. Deliberately absent are its goal actor (a raw pointer, restored as an index it would
// silently mis-aim), the player indices it holds grudges by (a slot may belong to somebody else
// now), and SCRIPTDATA_t, which is a paused interpreter -- a program counter, a call stack and a
// string stack that only mean anything against the exact script that was loaded.
//
// Header-pure by the features/ rules: no engine types, so this can be tested on the bytes alone.

#ifndef ZX_BOTSAVE_COMPUTE_H
#define ZX_BOTSAVE_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// Tier one, and nothing else. Every field here is a plain value that means the same thing on the
// way back in as it did on the way out.
struct BotSnapshot
{
	int slot;
	std::string name;
	std::string team;

	int forwardMove;
	int sideMove;
	bool forwardMovePersist;
	bool sideMovePersist;
	int buttons;
	bool aimAtEnemy;
	unsigned int aimAtEnemyDelay;
	unsigned int angleDelta;
	unsigned int angleOffBy;
	unsigned int angleDesired;
	bool turnLeft;
	unsigned int pathType;
	bool skillIncrease;
	bool skillDecrease;
	int lastMedalReceived;

	BotSnapshot()
		: slot(0), forwardMove(0), sideMove(0), forwardMovePersist(false), sideMovePersist(false),
		  buttons(0), aimAtEnemy(false), aimAtEnemyDelay(0), angleDelta(0), angleOffBy(0),
		  angleDesired(0), turnLeft(false), pathType(0), skillIncrease(false), skillDecrease(false),
		  lastMedalReceived(0) {}
};

// Bumped only when a field changes meaning. A chunk from a newer engine is REFUSED rather than read
// hopefully: half-understood bot state is worse than no bots, because no bots is a state the loader
// already handles correctly.
const int kBotSaveVersion = 1;

// The most a save may claim to hold, checked before anything is allocated or indexed. Bots live in
// player slots, so there can never be more of them than there are slots.
const int kBotSaveMaxBots = 64;

std::vector<unsigned char> SerialiseBots(const std::vector<BotSnapshot> &bots);

// False for anything that does not add up: wrong magic, a version we do not know, a count that runs
// past the end, a string longer than the bytes remaining. The caller must treat false as "no bots"
// and carry on loading, never as a reason to fail the load.
bool ParseBots(const unsigned char *data, size_t size, std::vector<BotSnapshot> &out);

} // namespace zx

#endif // ZX_BOTSAVE_COMPUTE_H
