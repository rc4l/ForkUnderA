// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Wire tests for SVC_CREATETRANSLATION, one range of an ACS translation (features/trnslate), written in the server's order
// (sv_commands.cpp) and read by the client's own reader, ComputeReadTranslationRange.

#include "doomtype.h"        // BYTE/USHORT and friends (must precede networkshared.h)
#include "networkshared.h"   // BYTESTREAM_s
#include "wire_nettest.h"     // zx::nettest::Wire
#include "network_enums.h"   // SVC_CREATETRANSLATION
#include "features/trnslate/computation/trnslate_compute.h"

#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>

using zx::nettest::Wire;
using namespace zx::trnslate;

namespace {

// [Doruk-Aksoy] The server's NetCommand calls, made on the test's stream.
struct Sink
{
	BYTESTREAM_s &w;
	void shortByte(int v, int bits) { w.WriteShortByte(v, bits); }
	void byte(int v) { w.WriteByte(v); }
	void longValue(int v) { w.WriteLong(v); }
	void floatValue(float v) { w.WriteFloat(v); }
};

// [Doruk-Aksoy] The client's reads, as client_CreateTranslation makes them.
struct Source
{
	BYTESTREAM_s &r;
	int shortByte(int bits) { return r.ReadShortByte(bits); }
	int byte() { return r.ReadByte(); }
	int longValue() { return r.ReadLong(); }
	float floatValue() { return r.ReadFloat(); }
};

void WriteCommand(Wire &wire, int index, bool edited, const TranslationRange &range)
{
	wire.w.WriteByte(SVC_CREATETRANSLATION);
	wire.w.WriteShort(index);
	wire.w.WriteBit(edited);
	Sink sink = { wire.w };
	ComputeWriteTranslationRange(sink, range);
}

// [Doruk-Aksoy] What the client reads after the command byte; false when the kind is unknown or the packet ran out.
bool ReadCommand(BYTESTREAM_s &r, int &index, bool &edited, TranslationRange &range)
{
	index = r.ReadShort();
	edited = r.ReadBit();
	Source source = { r };
	return ComputeReadTranslationRange(source, range) && !(r.pbStream > r.pbStreamEnd);
}

TranslationRange Colourise()
{
	TranslationRange range;
	range.kind = kRangeColourise;
	range.start = 16;
	range.end = 48;
	range.r1 = 255; range.g1 = 128; range.b1 = 300;
	return range;
}

} // namespace

TEST(TranslationWire, TheKindsKeepTheNumbersShippedClientsKnow)
{
	EXPECT_EQ(0, kRangePalette);
	EXPECT_EQ(1, kRangeRgb);
	EXPECT_EQ(2, kRangeDesaturated);
	EXPECT_EQ(3, kRangeColourise);
	EXPECT_EQ(4, kRangeTint);
	EXPECT_LE(kRangeKinds, 128);   // [Doruk-Aksoy] The kind is a 7-bit field.
}

TEST(TranslationWire, GoldenAColourise)
{
	Wire wire;
	WriteCommand(wire, 2, true, Colourise());
	const BYTE expected[] = {
		SVC_CREATETRANSLATION,
		0x02, 0x00,                // the translation
		0x07,                      // edited (bit 0), then kind 3 in the next seven bits
		16, 48,                    // the range
		0xFF, 0x00, 0x00, 0x00,    // r, a whole int
		0x80, 0x00, 0x00, 0x00,    // g
		0x2C, 0x01, 0x00, 0x00,    // b, 300 kept as UZDoom applies it
	};
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(TranslationWire, GoldenATintCarriesItsAmountFirst)
{
	TranslationRange range = Colourise();
	range.kind = kRangeTint;
	range.amount = -25;
	Wire wire;
	WriteCommand(wire, 1, false, range);
	const BYTE expected[] = {
		SVC_CREATETRANSLATION, 0x01, 0x00,
		0x08,                      // not edited, kind 4
		16, 48,
		0xE7, 0xFF, 0xFF, 0xFF,    // -25
		0xFF, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x2C, 0x01, 0x00, 0x00,
	};
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(TranslationWire, GoldenAPaletteRangeIsAsItWas)
{
	TranslationRange range;
	range.kind = kRangePalette;
	range.start = 112; range.end = 127; range.pal1 = 192; range.pal2 = 207;
	Wire wire;
	WriteCommand(wire, 3, false, range);
	const BYTE expected[] = { SVC_CREATETRANSLATION, 0x03, 0x00, 0x00, 112, 127, 192, 207 };
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(TranslationWire, EveryKindRoundTrips)
{
	for (int kind = kRangePalette; kind < kRangeKinds; kind++)
	{
		TranslationRange sent = Colourise();
		sent.kind = kind;
		sent.pal1 = 4; sent.pal2 = 9;
		sent.r2 = 10; sent.g2 = 20; sent.b2 = 30;
		sent.fr1 = 0.25f; sent.fg1 = 2.0f; sent.fb1 = 1.0f; sent.fr2 = 0.0f; sent.fg2 = 1.5f; sent.fb2 = 0.125f;
		sent.amount = 75;
		if (kind == kRangeRgb) sent.b1 = 44;   // [Doruk-Aksoy] An RGB range's colours are bytes, as they always were.
		Wire wire;
		WriteCommand(wire, 65535, true, sent);
		wire.openReader();
		ASSERT_EQ(SVC_CREATETRANSLATION, wire.r.ReadByte());
		int index = 0;
		bool edited = false;
		TranslationRange got;
		ASSERT_TRUE(ReadCommand(wire.r, index, edited, got)) << kind;
		EXPECT_EQ(65535, index & 0xFFFF);
		EXPECT_TRUE(edited);
		EXPECT_EQ(kind, got.kind);
		EXPECT_EQ(sent.start, got.start);
		EXPECT_EQ(sent.end, got.end);
		if (kind == kRangePalette) { EXPECT_EQ(4, got.pal1); EXPECT_EQ(9, got.pal2); }
		if (kind == kRangeRgb) { EXPECT_EQ(44, got.b1); EXPECT_EQ(30, got.b2); }
		if (kind == kRangeDesaturated) { EXPECT_EQ(0.25f, got.fr1); EXPECT_EQ(0.125f, got.fb2); }
		if (kind >= kRangeColourise) { EXPECT_EQ(255, got.r1); EXPECT_EQ(300, got.b1); }
		if (kind == kRangeTint) EXPECT_EQ(75, got.amount);
	}
}

TEST(TranslationWire, AnUnknownKindIsRefused)
{
	// [Doruk-Aksoy] A kind past this build's is not read as a desaturation's 24 bytes.
	Wire wire;
	wire.w.WriteByte(SVC_CREATETRANSLATION);
	wire.w.WriteShort(1);
	wire.w.WriteBit(false);
	wire.w.WriteShortByte(5, 7);
	wire.w.WriteByte(0);
	wire.w.WriteByte(10);
	wire.openReader();
	wire.r.ReadByte();
	int index = 0;
	bool edited = true;
	TranslationRange got;
	EXPECT_FALSE(ReadCommand(wire.r, index, edited, got));
}

TEST(TranslationWire, ATruncatedTintIsRefused)
{
	Wire full;
	TranslationRange range = Colourise();
	range.kind = kRangeTint;
	WriteCommand(full, 1, false, range);
	for (size_t cut = 1; cut < full.written(); cut++)
	{
		Wire wire;
		WriteCommand(wire, 1, false, range);
		wire.openReader(cut);
		wire.r.ReadByte();
		int index = 0;
		bool edited = false;
		TranslationRange got;
		EXPECT_FALSE(ReadCommand(wire.r, index, edited, got)) << cut;
	}
}
