// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Whether the client has to reload before joining the server it just started, and what to do
// when that cannot be answered.
//
// Hosting from the browser used to mean hosting what you were already running, so joining your own
// server was just a connect: the server's command line came from ours, and by construction there was
// nothing to check. A catalogue entry broke that. The server loads the ENTRY's files, the client is
// still running whatever it had, and the two only agree if somebody reloads.
//
// The reload was driven by a value filled in when the server started and cleared the first time it
// was used. That makes "did we remember" a question with a wrong answer available, and the wrong
// answer is silent: the client connects with the files it happens to have, Zandronum refuses it with
// PROTECTED LUMP AUTHENTICATION FAILED, and the player cannot tell that from a server that genuinely
// mismatches. It reads as the experience being broken.
//
// So the rule is stated here rather than as an `if` around a static, and the case it exists for is
// the last one: when we cannot work out what to reload onto, REFUSING is right and connecting is
// not. A refusal can say what went wrong; a blind connect produces an error about lumps that names
// nothing the player did.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_OWNJOIN_COMPUTE_H
#define ZX_OWNJOIN_COMPUTE_H

#include <string>

namespace zx
{

enum class OwnJoinAction
{
	// The server is running what we are running. This is the custom-setup case, and the one the
	// original straight-to-the-address join was written for.
	ConnectDirectly,

	// Reload onto the entry's files, then connect. The reload carries the connect with it.
	ReloadThenConnect,

	// Say why and stay put. Connecting from here cannot succeed.
	Refuse,
};

struct OwnJoinIn
{
	// Whether the running server was started from a catalogue entry rather than from the form.
	//
	// [rc4l] This no longer means "needs no reload" on its own, and reading it that way was a bug:
	// the NEW screen and the CUSTOM tab are not catalogue entries either, and both start servers on
	// files the client is not running. What decides is `haveRememberedFiles` below; this only says
	// whether a forgotten list can be rebuilt.
	bool hostingCatalogueEntry;

	// [rc4l] Whether the list the server was ACTUALLY HANDED is still in hand. The strongest of the
	// three and the one asked first: the other two are inferences about what the server is probably
	// running, and this is a record of what it was told to run.
	bool haveRememberedFiles;

	// Whether the list can be rebuilt from the entry now. False when a file has gone missing since
	// the server started, or when the entry names an IWAD this machine cannot supply.
	bool canRebuildFiles;

	OwnJoinIn() : hostingCatalogueEntry(false), haveRememberedFiles(false), canRebuildFiles(false) {}
};

struct OwnJoinOut
{
	OwnJoinAction action;

	// True when the caller must use the rebuilt list because the remembered one was gone. Only
	// meaningful for ReloadThenConnect.
	bool useRebuilt;

	// Why, when refusing. Never empty for Refuse, always empty otherwise.
	std::string refusal;

	OwnJoinOut() : action(OwnJoinAction::ConnectDirectly), useRebuilt(false) {}
};

OwnJoinOut DecideOwnJoin(const OwnJoinIn &in);

} // namespace zx

#endif // ZX_OWNJOIN_COMPUTE_H
