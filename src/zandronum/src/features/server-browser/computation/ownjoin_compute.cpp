// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/ownjoin_compute.h"

namespace zx
{

OwnJoinOut DecideOwnJoin(const OwnJoinIn &in)
{
	OwnJoinOut out;

	// [rc4l] THE LIST THE SERVER WAS ACTUALLY HANDED, asked FIRST -- before what kind of server it
	// is -- because it is the only one of these three that is evidence rather than inference.
	//
	// It used to be asked second, behind "a custom setup runs the client's own files, so connect
	// straight away". That premise was true when a custom setup meant the hosting FORM, whose server
	// inherited whatever this client had loaded. The NEW screen ended it: that builds a server out
	// of files picked from the library, and the CUSTOM tab starts a saved preset, while the client
	// goes on running whatever it booted with. Both handed the server a list, both then connected
	// without it, and Zandronum refused them with PROTECTED LUMP AUTHENTICATION FAILED naming files
	// the player had just chosen. Hosting your own preset could not be joined at all.
	//
	// Preferred over a rebuild too: a file replaced on disk between starting and joining would make
	// the rebuild right about the disk and wrong about the running server.
	if (in.haveRememberedFiles)
	{
		out.action = OwnJoinAction::ReloadThenConnect;
		return out;
	}

	// Nothing remembered and not an entry: the old custom-setup case, a server whose command line
	// really did come from ours. Reloading would tear the game down to arrive back where it started.
	if (!in.hostingCatalogueEntry)
	{
		out.action = OwnJoinAction::ConnectDirectly;
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
