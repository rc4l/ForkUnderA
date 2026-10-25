// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Wire tests for SVC_SOUNDACTOR carrying what overlapping and amplified sounds rely on: CHAN_OVERLAP in its channel short and a volume past 1 in its volume byte.
// [Doruk-Aksoy] Modelled on BYTESTREAM_s in the generator's field order (protocolspec/spec.sounds.txt), since the generated dispatcher cannot be unit-linked (netcode-testing skill).

#include "doomtype.h"        // BYTE/USHORT and friends (must precede networkshared.h)
#include "networkshared.h"   // BYTESTREAM_s
#include "network_enums.h"   // SVC_SOUNDACTOR
#include "features/sound-amplify/computation/sound_amplify_compute.h"
#include "features/sound-overlap/computation/sound_overlap_compute.h"

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

namespace {

// [Doruk-Aksoy] Mirrors s_sound.h's CHAN_BODY.
const int kChanBody = 4;

// [Doruk-Aksoy] Every modifier flag the engine packs into a channel, all set at once.
const int kAllFlags = 8 | 16 | 32 | 64 | 128 | ZX_CHAN_LOOP | ZX_CHAN_NOSTOP | ZX_CHAN_OVERLAP;

// [Doruk-Aksoy] The command's fields, in spec order.
struct Payload {
	int netId;
	int channel;
	std::string sound;
	int volume;
	int attenuation;
	int pitch;
};

// [Doruk-Aksoy] Writer and reader views over the same poisoned bytes, mirroring server-writes / client-reads.
struct Wire {
	std::vector<BYTE> buf;
	BYTESTREAM_s w{}, r{};
	explicit Wire(size_t n = 128) : buf(n, 0xCC) {
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

// [Doruk-Aksoy] What the generated SoundActor::BuildNetCommand writes, header first.
void WriteCommand(Wire &wire, const Payload &p) {
	wire.w.WriteByte(SVC_SOUNDACTOR);
	wire.w.WriteShort(p.netId);
	wire.w.WriteShort(p.channel);
	wire.w.WriteString(p.sound.c_str());
	wire.w.WriteByte(p.volume);
	wire.w.WriteByte(p.attenuation);
	wire.w.WriteShort(p.pitch);
}

// [Doruk-Aksoy] What the generated parser reads after the client has consumed the header byte.
Payload ReadPayload(BYTESTREAM_s &r) {
	Payload p;
	p.netId = static_cast<unsigned short>(r.ReadShort());
	p.channel = r.ReadShort();
	p.sound = r.ReadString();
	p.volume = r.ReadByte();
	p.attenuation = r.ReadByte();
	p.pitch = r.ReadShort();
	return p;
}

// [Doruk-Aksoy] An amplified loop overlapping on CHAN_BODY, at the server's largest volume and default pitch.
Payload OverlappingLoop() {
	Payload p = { 0x1234, kChanBody | ZX_CHAN_LOOP | ZX_CHAN_OVERLAP, "fua/tone",
		ComputeSoundVolumeToWire(ZX_MAX_AMPLIFIED_VOLUME), 1, -1 };
	return p;
}

} // namespace

TEST(SoundActorWire, GoldenOverlappingAmplifiedLoop) {
	Wire wire;
	WriteCommand(wire, OverlappingLoop());

	const BYTE expected[] = {
		static_cast<BYTE>(SVC_SOUNDACTOR),
		0x34, 0x12,                             // actor's net id
		0x04, 0x21,                             // channel 0x2104: CHAN_BODY | CHAN_LOOP | CHAN_OVERLAP
		'f', 'u', 'a', '/', 't', 'o', 'n', 'e', 0,
		0xFE,                                   // volume 254, i.e. 2.0
		0x01,                                   // attenuation
		0xFF, 0xFF,                             // pitch -1: the client picks its own
	};
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(SoundActorWire, RoundTripKeepsTheSlotFlagsAndVolume) {
	Wire wire;
	WriteCommand(wire, OverlappingLoop());
	wire.openReader();
	ASSERT_EQ(SVC_SOUNDACTOR, wire.r.ReadByte());
	const Payload p = ReadPayload(wire.r);

	EXPECT_EQ(0x1234, p.netId);
	EXPECT_EQ(kChanBody, p.channel & 7);
	EXPECT_EQ(ZX_CHAN_LOOP | ZX_CHAN_OVERLAP, p.channel & ~7);
	EXPECT_EQ("fua/tone", p.sound);
	EXPECT_FLOAT_EQ(ZX_MAX_AMPLIFIED_VOLUME, ComputeSoundVolumeFromWire(p.volume));
	EXPECT_EQ(-1, p.pitch);
}

TEST(SoundActorWire, EveryFlagFitsTheChannelShort) {
	// [Doruk-Aksoy] A signed short holds slot 7 with every flag, so no flag reaches the sign bit.
	Wire wire;
	wire.w.WriteShort(7 | kAllFlags);
	wire.openReader();
	EXPECT_EQ(7 | kAllFlags, wire.r.ReadShort());
	EXPECT_LT(7 | kAllFlags, 0x8000);
}

TEST(SoundActorWire, VolumeByteIsReadUnsigned) {
	// [Doruk-Aksoy] Every byte past 127 stays past 1, which a signed read would have turned negative and silent.
	for (int b = 128; b <= 255; ++b) {
		Wire wire;
		wire.w.WriteByte(b);
		wire.openReader();
		const int read = wire.r.ReadByte();
		EXPECT_EQ(b, read);
		EXPECT_GT(ComputeSoundVolumeFromWire(read), 1.f);
	}
}

TEST(SoundActorWire, TruncatedBeforeTheVolumeReadsAsNoSound) {
	// [Doruk-Aksoy] A packet cut after the name gives a volume of -1, which plays nothing rather than a garbage level.
	Wire wire;
	WriteCommand(wire, OverlappingLoop());
	const size_t beforeVolume = 1 + 2 + 2 + std::strlen("fua/tone") + 1;
	wire.openReader(beforeVolume);
	ASSERT_EQ(SVC_SOUNDACTOR, wire.r.ReadByte());
	const Payload p = ReadPayload(wire.r);
	EXPECT_EQ(-1, p.volume);
	EXPECT_FLOAT_EQ(0.f, ComputeSoundVolume(ComputeSoundVolumeFromWire(p.volume), 1.f, true));
}
