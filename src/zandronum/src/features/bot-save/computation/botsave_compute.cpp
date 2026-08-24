// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/bot-save/computation/botsave_compute.h"

#include <cstring>

namespace zx
{

namespace
{

const char kMagic[4] = { 'B', 'O', 'T', 'S' };

void PutU32(std::vector<unsigned char> &out, unsigned int v)
{
	for (int i = 0; i < 4; ++i)
		out.push_back(static_cast<unsigned char>((v >> (i * 8)) & 0xFF));
}

void PutString(std::vector<unsigned char> &out, const std::string &s)
{
	// Length-prefixed rather than terminated: a name is whatever the player typed, and a scan for a
	// zero byte is a scan that can run off the end of a truncated chunk.
	PutU32(out, static_cast<unsigned int>(s.size()));
	out.insert(out.end(), s.begin(), s.end());
}

// Every read checks first, so a truncated or hostile chunk ends the parse instead of walking off
// the buffer. Same shape as the geo table's reader, and for the same reason.
struct Reader
{
	const unsigned char *p;
	const unsigned char *end;
	bool ok;

	Reader(const unsigned char *data, size_t size) : p(data), end(data + size), ok(true) {}

	unsigned int U32()
	{
		if ((ok == false) || (end - p < 4))
			return (ok = false), 0u;

		unsigned int v = 0;
		for (int i = 0; i < 4; ++i)
			v |= static_cast<unsigned int>(p[i]) << (i * 8);
		p += 4;
		return v;
	}

	bool Str(std::string &out)
	{
		const unsigned int len = U32();
		if (ok == false)
			return false;

		if (static_cast<size_t>(end - p) < static_cast<size_t>(len))
			return (ok = false), false;

		out.assign(reinterpret_cast<const char *>(p), len);
		p += len;
		return true;
	}
};

} // namespace

std::vector<unsigned char> SerialiseBots(const std::vector<BotSnapshot> &bots)
{
	std::vector<unsigned char> out;
	if (bots.empty())
		return out;			// no chunk at all rather than a chunk saying nothing

	out.insert(out.end(), kMagic, kMagic + sizeof kMagic);
	PutU32(out, static_cast<unsigned int>(kBotSaveVersion));
	PutU32(out, static_cast<unsigned int>(bots.size()));

	for (size_t i = 0; i < bots.size(); ++i)
	{
		const BotSnapshot &b = bots[i];

		PutU32(out, static_cast<unsigned int>(b.slot));
		PutString(out, b.name);
		PutString(out, b.team);

		PutU32(out, static_cast<unsigned int>(b.forwardMove));
		PutU32(out, static_cast<unsigned int>(b.sideMove));
		PutU32(out, b.forwardMovePersist ? 1u : 0u);
		PutU32(out, b.sideMovePersist ? 1u : 0u);
		PutU32(out, static_cast<unsigned int>(b.buttons));
		PutU32(out, b.aimAtEnemy ? 1u : 0u);
		PutU32(out, b.aimAtEnemyDelay);
		PutU32(out, b.angleDelta);
		PutU32(out, b.angleOffBy);
		PutU32(out, b.angleDesired);
		PutU32(out, b.turnLeft ? 1u : 0u);
		PutU32(out, b.pathType);
		PutU32(out, b.skillIncrease ? 1u : 0u);
		PutU32(out, b.skillDecrease ? 1u : 0u);
		PutU32(out, static_cast<unsigned int>(b.lastMedalReceived));
	}

	return out;
}

bool ParseBots(const unsigned char *data, size_t size, std::vector<BotSnapshot> &out)
{
	out.clear();

	if ((data == 0) || (size < sizeof kMagic) || (memcmp(data, kMagic, sizeof kMagic) != 0))
		return false;

	Reader in(data + sizeof kMagic, size - sizeof kMagic);

	const unsigned int version = in.U32();
	if ((in.ok == false) || (version != static_cast<unsigned int>(kBotSaveVersion)))
		return false;

	const unsigned int count = in.U32();
	if ((in.ok == false) || (count > static_cast<unsigned int>(kBotSaveMaxBots)))
		return false;

	for (unsigned int i = 0; i < count; ++i)
	{
		BotSnapshot b;

		b.slot = static_cast<int>(in.U32());
		if ((in.Str(b.name) == false) || (in.Str(b.team) == false))
			return (out.clear(), false);

		b.forwardMove = static_cast<int>(in.U32());
		b.sideMove = static_cast<int>(in.U32());
		b.forwardMovePersist = (in.U32() != 0);
		b.sideMovePersist = (in.U32() != 0);
		b.buttons = static_cast<int>(in.U32());
		b.aimAtEnemy = (in.U32() != 0);
		b.aimAtEnemyDelay = in.U32();
		b.angleDelta = in.U32();
		b.angleOffBy = in.U32();
		b.angleDesired = in.U32();
		b.turnLeft = (in.U32() != 0);
		b.pathType = in.U32();
		b.skillIncrease = (in.U32() != 0);
		b.skillDecrease = (in.U32() != 0);
		b.lastMedalReceived = static_cast<int>(in.U32());

		if (in.ok == false)
			return (out.clear(), false);

		out.push_back(b);
	}

	return true;
}

} // namespace zx
