// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida
//
// [KI] The sv_dumppackets decisions, mirroring 3.3's SERVER_ShouldDumpServerCommand, SERVER_ShouldDumpClientCommand and SERVER_DumpPacket naming; the cvar reads stay in sv_main.cpp's glue.

#include "dumpfilter_compute.h"
#include "network_enums.h"
#include <set>

namespace zx { namespace netsession {

//*****************************************************************************
//
bool ComputeShouldDumpServerCommand(int command, int extcommand)
{
	// [KI] 3.3's set minus the three SVC2_SRP_* entries, since ForkUnderA removed the auth server those commands belonged to.
	static const std::set<int> ignoredCommands
	{
		SVC_SETPLAYERUSERINFO,
		SVC_PLAYERSAY,
		SVC_PLAYERVOIPAUDIOPACKET,
		SVC_PRINT,
		SVC_PRINTMID,
		SVC_PRINTMOTD,
		SVC_PRINTHUDMESSAGE,
	};

	static const std::set<int> ignoredExtCommands
	{
		SVC2_SETPLAYERACCOUNTNAME,
		SVC2_ACSSENDSTRING,
		SVC2_RCONACCESS,
	};

	if (command == SVC_EXTENDEDCOMMAND)
		return !ignoredExtCommands.count(extcommand);

	return !ignoredCommands.count(command);
}

//*****************************************************************************
//
bool ComputeShouldDumpClientCommand(int command)
{
	if (command < static_cast<int>(NUM_CLIENTCONNECT_COMMANDS) || command >= NUM_CLIENT_COMMANDS)
		return false;

	static const std::set<int> ignoredCommands
	{
		CLC_SAY,
		CLC_CHANGERCONSTATUS,
		CLC_RCONCOMMAND,
		CLC_ACSSENDSTRING,
		CLC_RCONSETCVAR,
		CLC_VOIPAUDIOPACKET,
	};

	return !ignoredCommands.count(command);
}

//*****************************************************************************
//
unsigned int ComputeDumpCounter(bool sameTimestamp, unsigned int counter)
{
	return sameTimestamp ? counter + 1 : 0;
}

//*****************************************************************************
//
std::string ComputeDumpFileName(const std::string &dir, bool fromClient, const std::string &timestamp, unsigned int counter)
{
	return dir + "/packet_" + (fromClient ? "cl" : "sv") + "_" + timestamp + "_" + std::to_string(counter) + ".bin";
}

}} // namespace zx::netsession
