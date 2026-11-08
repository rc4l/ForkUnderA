// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida
//
// [KI] Every branch of the sv_dumppackets decisions: the two ignore sets, the extended-command split, the connect-range gate, the collision counter and the file name.

#include "dumpfilter_compute.h"
#include "network_enums.h"
#include <gtest/gtest.h>

namespace {

using zx::netsession::ComputeDumpCounter;
using zx::netsession::ComputeDumpFileName;
using zx::netsession::ComputeShouldDumpClientCommand;
using zx::netsession::ComputeShouldDumpServerCommand;

//*****************************************************************************
//
TEST(DumpFilterCompute, OrdinaryServerCommandsDump)
{
	EXPECT_TRUE(ComputeShouldDumpServerCommand(SVC_SPAWNTHING, 0));
}

TEST(DumpFilterCompute, IgnoredServerCommandsDoNot)
{
	EXPECT_FALSE(ComputeShouldDumpServerCommand(SVC_SETPLAYERUSERINFO, 0));
	EXPECT_FALSE(ComputeShouldDumpServerCommand(SVC_PLAYERSAY, 0));
	EXPECT_FALSE(ComputeShouldDumpServerCommand(SVC_PLAYERVOIPAUDIOPACKET, 0));
	EXPECT_FALSE(ComputeShouldDumpServerCommand(SVC_PRINT, 0));
	EXPECT_FALSE(ComputeShouldDumpServerCommand(SVC_PRINTMID, 0));
	EXPECT_FALSE(ComputeShouldDumpServerCommand(SVC_PRINTMOTD, 0));
	EXPECT_FALSE(ComputeShouldDumpServerCommand(SVC_PRINTHUDMESSAGE, 0));
}

TEST(DumpFilterCompute, ExtendedCommandsJudgeTheSecondByte)
{
	EXPECT_FALSE(ComputeShouldDumpServerCommand(SVC_EXTENDEDCOMMAND, SVC2_SETPLAYERACCOUNTNAME));
	EXPECT_FALSE(ComputeShouldDumpServerCommand(SVC_EXTENDEDCOMMAND, SVC2_ACSSENDSTRING));
	EXPECT_FALSE(ComputeShouldDumpServerCommand(SVC_EXTENDEDCOMMAND, SVC2_RCONACCESS));
	EXPECT_TRUE(ComputeShouldDumpServerCommand(SVC_EXTENDEDCOMMAND, SVC2_FULLUPDATECOMPLETED));
}

//*****************************************************************************
//
TEST(DumpFilterCompute, ConnectCommandsNeverDump)
{
	// [KI] Everything below NUM_CLIENTCONNECT_COMMANDS is connect-phase traffic, which travels as Huffman and teaches the dictionary nothing.
	EXPECT_FALSE(ComputeShouldDumpClientCommand(0));
	EXPECT_FALSE(ComputeShouldDumpClientCommand(static_cast<int>(NUM_CLIENTCONNECT_COMMANDS) - 1));
}

TEST(DumpFilterCompute, OutOfRangeClientCommandsNeverDump)
{
	EXPECT_FALSE(ComputeShouldDumpClientCommand(NUM_CLIENT_COMMANDS));
	EXPECT_FALSE(ComputeShouldDumpClientCommand(NUM_CLIENT_COMMANDS + 50));
}

TEST(DumpFilterCompute, OrdinaryClientCommandsDump)
{
	EXPECT_TRUE(ComputeShouldDumpClientCommand(CLC_CLIENTMOVE));
}

TEST(DumpFilterCompute, IgnoredClientCommandsDoNot)
{
	EXPECT_FALSE(ComputeShouldDumpClientCommand(CLC_SAY));
	EXPECT_FALSE(ComputeShouldDumpClientCommand(CLC_CHANGERCONSTATUS));
	EXPECT_FALSE(ComputeShouldDumpClientCommand(CLC_RCONCOMMAND));
	EXPECT_FALSE(ComputeShouldDumpClientCommand(CLC_ACSSENDSTRING));
	EXPECT_FALSE(ComputeShouldDumpClientCommand(CLC_RCONSETCVAR));
	EXPECT_FALSE(ComputeShouldDumpClientCommand(CLC_VOIPAUDIOPACKET));
}

//*****************************************************************************
//
TEST(DumpFilterCompute, CounterStepsWithinASecondAndResetsAcrossOne)
{
	EXPECT_EQ(1u, ComputeDumpCounter(true, 0));
	EXPECT_EQ(8u, ComputeDumpCounter(true, 7));
	EXPECT_EQ(0u, ComputeDumpCounter(false, 7));
}

TEST(DumpFilterCompute, FileNameMatchesThreePointThreesPattern)
{
	EXPECT_EQ("dumps/packet_cl_2026-11-08_10-00-00_0.bin",
		ComputeDumpFileName("dumps", true, "2026-11-08_10-00-00", 0));
	EXPECT_EQ("dumps/packet_sv_2026-11-08_10-00-00_3.bin",
		ComputeDumpFileName("dumps", false, "2026-11-08_10-00-00", 3));
}

} // namespace
