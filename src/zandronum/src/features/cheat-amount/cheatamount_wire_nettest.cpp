// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Wire tests for CLC_GIVECHEAT/CLC_TAKECHEAT, modelled on CLIENTCOMMANDS_GiveCheat and server_GiveCheat since neither unit-links.

#include "doomtype.h"        // BYTE/USHORT and friends (must precede networkshared.h)
#include "networkshared.h"   // BYTESTREAM_s
#include "network_enums.h"   // CLC_GIVECHEAT, CLC_TAKECHEAT
#include "features/cheat-amount/computation/cheatamount_compute.h"

#include <gtest/gtest.h>
#include <climits>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

using namespace zx::cheatamount;

namespace {

// [Doruk-Aksoy] Writer and reader views over the same poisoned bytes.
struct Wire {
	std::vector<BYTE> buf;
	BYTESTREAM_s w{}, r{};
	explicit Wire(size_t n = 64) : buf(n, 0xCC) {
		w.pbStream = buf.data();
		w.pbStreamEnd = buf.data() + buf.size();
	}
	size_t written() const { return static_cast<size_t>(w.pbStream - buf.data()); }
	void openReader(size_t n) {
		r = BYTESTREAM_s{};
		r.pbStream = buf.data();
		r.pbStreamEnd = buf.data() + n;
	}
	void openReader() { openReader(written()); }
};

// [Doruk-Aksoy] What CLIENTCOMMANDS_GiveCheat/TakeCheat write for the console arguments give/take hands them.
void WriteCommand(Wire &wire, int header, const char *item, const char *amountArg) {
	wire.w.WriteByte(header);
	wire.w.WriteString(item);
	wire.w.WriteLong(ComputeCheatAmountArg(amountArg));
}

// [Doruk-Aksoy] What server_GiveCheat applies after the header byte, as item and amount.
struct Received {
	std::string item;
	int amount;
};

Received ReadCommand(BYTESTREAM_s &r) {
	Received rec;
	rec.item = r.ReadString();
	rec.amount = ComputeCheatAmountFromWire(r.ReadLong());
	return rec;
}

} // namespace

TEST(CheatAmountWire, GiveGoldenBytes)
{
	Wire wire;
	WriteCommand(wire, CLC_GIVECHEAT, "Clip", "100000");
	const std::vector<BYTE> expected = {
		static_cast<BYTE>(CLC_GIVECHEAT),
		'C', 'l', 'i', 'p', 0x00,
		0xA0, 0x86, 0x01, 0x00, // [Doruk-Aksoy] 100000 little-endian.
	};
	ASSERT_EQ(wire.written(), expected.size());
	EXPECT_EQ(std::vector<BYTE>(wire.buf.begin(), wire.buf.begin() + wire.written()), expected);
}

TEST(CheatAmountWire, TakeWithoutAmountSendsZeroLong)
{
	Wire wire;
	WriteCommand(wire, CLC_TAKECHEAT, "health", nullptr);
	const std::vector<BYTE> expected = {
		static_cast<BYTE>(CLC_TAKECHEAT),
		'h', 'e', 'a', 'l', 't', 'h', 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	ASSERT_EQ(wire.written(), expected.size());
	EXPECT_EQ(std::vector<BYTE>(wire.buf.begin(), wire.buf.begin() + wire.written()), expected);
}

TEST(CheatAmountWire, AmountsPastTheOldByteCapRoundTrip)
{
	const struct { const char *arg; int want; } cases[] = {
		{ nullptr, 0 }, { "1", 1 }, { "255", 255 }, { "256", 256 }, { "32768", 32768 },
		{ "2147483647", INT_MAX }, { "99999999999", INT_MAX }, { "-7", 1 },
	};
	for (const auto &c : cases)
	{
		Wire wire;
		WriteCommand(wire, CLC_GIVECHEAT, "Cell", c.arg);
		wire.openReader();
		ASSERT_EQ(wire.r.ReadByte(), CLC_GIVECHEAT);
		const Received rec = ReadCommand(wire.r);
		EXPECT_EQ(rec.item, "Cell");
		EXPECT_EQ(rec.amount, c.want) << (c.arg ? c.arg : "(none)");
		EXPECT_EQ(wire.r.pbStream, wire.r.pbStreamEnd);
	}
}

TEST(CheatAmountWire, NextCommandStaysAligned)
{
	Wire wire;
	WriteCommand(wire, CLC_GIVECHEAT, "Shell", "300");
	WriteCommand(wire, CLC_TAKECHEAT, "Shell", "40");
	wire.openReader();
	ASSERT_EQ(wire.r.ReadByte(), CLC_GIVECHEAT);
	EXPECT_EQ(ReadCommand(wire.r).amount, 300);
	ASSERT_EQ(wire.r.ReadByte(), CLC_TAKECHEAT);
	const Received rec = ReadCommand(wire.r);
	EXPECT_EQ(rec.item, "Shell");
	EXPECT_EQ(rec.amount, 40);
}

TEST(CheatAmountWire, TruncatedAmountIsNoneGivenAndStaysInBounds)
{
	Wire wire;
	WriteCommand(wire, CLC_GIVECHEAT, "Clip", "100000");
	// [Doruk-Aksoy] Header, "Clip\0", then only 0..3 of the amount's four bytes.
	for (size_t amountBytes = 0; amountBytes < 4; ++amountBytes)
	{
		wire.openReader(1 + 5 + amountBytes);
		ASSERT_EQ(wire.r.ReadByte(), CLC_GIVECHEAT);
		const Received rec = ReadCommand(wire.r);
		EXPECT_EQ(rec.item, "Clip");
		EXPECT_EQ(rec.amount, 0) << amountBytes;
	}
}

TEST(CheatAmountWire, HostileNegativeAmountIsNoneGiven)
{
	Wire wire;
	wire.w.WriteByte(CLC_TAKECHEAT);
	wire.w.WriteString("Clip");
	wire.w.WriteLong(INT_MIN);
	wire.openReader();
	ASSERT_EQ(wire.r.ReadByte(), CLC_TAKECHEAT);
	EXPECT_EQ(ReadCommand(wire.r).amount, 0);
}
