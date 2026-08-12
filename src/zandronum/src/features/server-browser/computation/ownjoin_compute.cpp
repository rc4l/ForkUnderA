// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/ownjoin_compute.h"

namespace zx
{

OwnJoinOut DecideOwnJoin(const OwnJoinIn &in)
{
	OwnJoinOut out;

	// A custom setup runs the client's own files: that is where the server's command line came from.
	// Reloading would tear the game down to arrive back where it started.
	if (!in.hostingCatalogueEntry)
	{
		out.action = OwnJoinAction::ConnectDirectly;
		return out;
	}

	// The list the server was ACTUALLY handed, which is the one to prefer. A rebuild should agree
	// with it and does not have to: a file replaced on disk between starting and joining would make
	// the rebuild right about the disk and wrong about the running server.
	if (in.haveRememberedFiles)
	{
		out.action = OwnJoinAction::ReloadThenConnect;
		return out;
	}

	if (in.canRebuildFiles)
	{
		out.action = OwnJoinAction::ReloadThenConnect;
		out.useRebuilt = true;
		return out;
	}

	// [rc4l] THE case this unit exists for. Connecting from here is not a gamble that might come off:
	// the server is running an entry's files, the client is not, and authentication compares them. It
	// fails every time, and it fails with a message about protected lumps that names nothing the
	// player chose.
	out.action = OwnJoinAction::Refuse;
	out.refusal = "the files this experience needs could not be found, so joining would fail "
		"authentication. The server is still running; check the console for which file is missing.";
	return out;
}

} // namespace zx
