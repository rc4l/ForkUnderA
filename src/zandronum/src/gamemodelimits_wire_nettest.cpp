// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Wire tests for SVC_SETGAMEMODELIMITS, the game mode limits and gameplay settings a server sends, in SERVERCOMMANDS_SetGameModeLimits' field order.
// [rc4l] The command is hand-written and reads live CVARs, so its layout is modelled here on BYTESTREAM_s (netcode-testing skill).

#include "doomtype.h"        // BYTE/USHORT and friends (must precede networkshared.h)
#include "networkshared.h"   // BYTESTREAM_s
#include "network_enums.h"   // SVC_SETGAMEMODELIMITS

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

namespace {

// [rc4l] Every field SetGameModeLimits sends, in the order it sends them.
struct Limits {
	int fraglimit; float timelimit; int pointlimit;
	int duellimit, winlimit, wavelimit, cheats, fastweapons, maxlives, maxteams;
	float gravity, aircontrol, coopDamageFactor;
	bool alwaysApplyDmflags, unlimitedPickup, limitCommands, respawnInSurvivalInvasion;
	std::string lobby;
	int allowPrivateChat, allowVoiceChat, proximityVoiceChat;
	float minProximityRolloff, maxProximityRolloff, respawnDelayTime;
	int noCallVote;
	float minFov, maxFov;
};

Limits Sample() {
	Limits l{};
	l.fraglimit = 20; l.timelimit = 10.f; l.maxteams = 2;
	l.gravity = 800.f; l.aircontrol = 1.f / 256.f; l.coopDamageFactor = 1.f;
	l.alwaysApplyDmflags = true; l.limitCommands = true; l.respawnInSurvivalInvasion = true;
	l.allowPrivateChat = 1;
	l.minProximityRolloff = 200.f; l.maxProximityRolloff = 1200.f; l.respawnDelayTime = 1.f;
	l.minFov = 60.f; l.maxFov = 120.f;
	return l;
}

struct Wire {
	std::vector<BYTE> buf;
	BYTESTREAM_s w{}, r{};
	explicit Wire(size_t n = 256) : buf(n, 0xCC) {
		w.pbStream = buf.data();
		w.pbStreamEnd = buf.data() + buf.size();
	}
	size_t written() const { return static_cast<size_t>(w.pbStream - buf.data()); }
	void openReader(size_t truncateTo = static_cast<size_t>(-1)) {
		const size_t n = (truncateTo == static_cast<size_t>(-1)) ? written() : truncateTo;
		r = BYTESTREAM_s{};
		r.pbStream = buf.data();
		r.pbStreamEnd = buf.data() + n;
	}
};

// [rc4l] What SERVERCOMMANDS_SetGameModeLimits writes, header first.
void WriteCommand(Wire &wire, const Limits &l) {
	BYTESTREAM_s &w = wire.w;
	w.WriteByte(SVC_SETGAMEMODELIMITS);
	w.WriteShort(l.fraglimit);
	w.WriteFloat(l.timelimit);
	w.WriteShort(l.pointlimit);
	w.WriteByte(l.duellimit);
	w.WriteByte(l.winlimit);
	w.WriteByte(l.wavelimit);
	w.WriteByte(l.cheats);
	w.WriteByte(l.fastweapons);
	w.WriteByte(l.maxlives);
	w.WriteByte(l.maxteams);
	w.WriteFloat(l.gravity);
	w.WriteFloat(l.aircontrol);
	w.WriteFloat(l.coopDamageFactor);
	w.WriteBit(l.alwaysApplyDmflags);
	w.WriteBit(l.unlimitedPickup);
	w.WriteBit(l.limitCommands);
	w.WriteBit(l.respawnInSurvivalInvasion);
	w.WriteString(l.lobby.c_str());
	w.WriteByte(l.allowPrivateChat);
	w.WriteByte(l.allowVoiceChat);
	w.WriteByte(l.proximityVoiceChat);
	w.WriteFloat(l.minProximityRolloff);
	w.WriteFloat(l.maxProximityRolloff);
	w.WriteFloat(l.respawnDelayTime);
	w.WriteByte(l.noCallVote);
	w.WriteFloat(l.minFov);
	w.WriteFloat(l.maxFov);
}

// [rc4l] What client_SetGameModeLimits reads after the header byte; false when it ran past the packet.
bool ReadCommand(BYTESTREAM_s &r, Limits &l) {
	l.fraglimit = r.ReadShort();
	l.timelimit = r.ReadFloat();
	l.pointlimit = r.ReadShort();
	l.duellimit = r.ReadByte();
	l.winlimit = r.ReadByte();
	l.wavelimit = r.ReadByte();
	l.cheats = r.ReadByte();
	l.fastweapons = r.ReadByte();
	l.maxlives = r.ReadByte();
	l.maxteams = r.ReadByte();
	l.gravity = r.ReadFloat();
	l.aircontrol = r.ReadFloat();
	l.coopDamageFactor = r.ReadFloat();
	l.alwaysApplyDmflags = r.ReadBit();
	l.unlimitedPickup = r.ReadBit();
	l.limitCommands = r.ReadBit();
	l.respawnInSurvivalInvasion = r.ReadBit();
	l.lobby = r.ReadString();
	l.allowPrivateChat = r.ReadByte();
	l.allowVoiceChat = r.ReadByte();
	l.proximityVoiceChat = r.ReadByte();
	l.minProximityRolloff = r.ReadFloat();
	l.maxProximityRolloff = r.ReadFloat();
	l.respawnDelayTime = r.ReadFloat();
	l.noCallVote = r.ReadByte();
	l.minFov = r.ReadFloat();
	l.maxFov = r.ReadFloat();
	return !(r.pbStream > r.pbStreamEnd);
}

} // namespace

TEST(SetGameModeLimitsWire, Golden) {
	Wire wire;
	WriteCommand(wire, Sample());
	const BYTE expected[] = {
		static_cast<BYTE>(SVC_SETGAMEMODELIMITS),
		0x14, 0x00,              // fraglimit 20
		0x00, 0x00, 0x20, 0x41,  // timelimit 10.0f
		0x00, 0x00,              // pointlimit
		0, 0, 0, 0, 0, 0, 2,     // duel, win, wave, cheats, fastweapons, maxlives; maxteams 2
		0x00, 0x00, 0x48, 0x44,  // gravity 800.0f
		0x00, 0x00, 0x80, 0x3B,  // aircontrol 1/256
		0x00, 0x00, 0x80, 0x3F,  // sv_coop_damagefactor 1.0f
		0x0D,                    // the four bits in one byte, first in bit 0: 1, 0, 1, 1
		0x00,                    // an empty lobby
		1, 0, 0,                 // private chat, voice chat, proximity voice chat
		0x00, 0x00, 0x48, 0x43,  // min proximity rolloff 200.0f
		0x00, 0x00, 0x96, 0x44,  // max proximity rolloff 1200.0f
		0x00, 0x00, 0x80, 0x3F,  // sv_respawndelaytime 1.0f
		0,                       // sv_nocallvote
		0x00, 0x00, 0x70, 0x42,  // sv_minfov 60.0f (zandronum@7b3e7adb1460)
		0x00, 0x00, 0xF0, 0x42,  // sv_maxfov 120.0f
	};
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(SetGameModeLimitsWire, RoundTripKeepsEveryField) {
	Limits sent = Sample();
	sent.lobby = "LOBBY01";
	sent.unlimitedPickup = true;
	sent.minFov = 5.5f;
	sent.maxFov = 179.f;
	Wire wire;
	WriteCommand(wire, sent);
	wire.openReader();
	ASSERT_EQ(SVC_SETGAMEMODELIMITS, wire.r.ReadByte());
	Limits got{};
	ASSERT_TRUE(ReadCommand(wire.r, got));
	EXPECT_EQ(sent.fraglimit, got.fraglimit);
	EXPECT_EQ(sent.timelimit, got.timelimit);
	EXPECT_EQ(sent.maxteams, got.maxteams);
	EXPECT_EQ(sent.gravity, got.gravity);
	EXPECT_EQ(sent.aircontrol, got.aircontrol);
	EXPECT_EQ(sent.alwaysApplyDmflags, got.alwaysApplyDmflags);
	EXPECT_EQ(sent.unlimitedPickup, got.unlimitedPickup);
	EXPECT_EQ(sent.limitCommands, got.limitCommands);
	EXPECT_EQ(sent.respawnInSurvivalInvasion, got.respawnInSurvivalInvasion);
	EXPECT_EQ(sent.lobby, got.lobby);
	EXPECT_EQ(sent.allowPrivateChat, got.allowPrivateChat);
	EXPECT_EQ(sent.maxProximityRolloff, got.maxProximityRolloff);
	EXPECT_EQ(sent.respawnDelayTime, got.respawnDelayTime);
	EXPECT_EQ(sent.noCallVote, got.noCallVote);
	EXPECT_EQ(sent.minFov, got.minFov);
	EXPECT_EQ(sent.maxFov, got.maxFov);
	EXPECT_EQ(wire.written(), static_cast<size_t>(wire.r.pbStream - wire.buf.data()));
}

TEST(SetGameModeLimitsWire, PacketCutInsideTheFovLimitsIsCaught) {
	// [rc4l] A server older than the FOV limits stops after sv_nocallvote, which a newer client reads past.
	Wire full;
	WriteCommand(full, Sample());
	for (size_t cut = full.written() - 8; cut < full.written(); ++cut) {
		Wire wire;
		WriteCommand(wire, Sample());
		wire.openReader(cut);
		wire.r.ReadByte();
		Limits got{};
		EXPECT_FALSE(ReadCommand(wire.r, got)) << cut;
	}
}
