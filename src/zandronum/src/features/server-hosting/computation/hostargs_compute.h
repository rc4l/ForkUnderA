// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The command line for the server we are about to become responsible for.
//
// Built as a VECTOR OF ARGUMENTS, never as a string. Everything here comes from a player typing into
// a menu -- a server name, a map, a list of WAD filenames off their own disk -- and the moment those
// are pasted into one line, the shell's quoting rules decide what the engine receives. A server
// called `Bob"s -exec autoexec.cfg` is a joke a player can make by accident. A vector has no quoting
// rules to get wrong, because there is no parsing step: each element arrives as one argument no
// matter what is in it.
//
// Windows spoils that slightly -- CreateProcess only takes a string, so someone has to re-quote --
// which is exactly why the quoting lives here, one implementation, tested, instead of at the call
// site where it would be improvised.
//
// WHAT THIS DELIBERATELY DOES NOT DO: it never passes a value through that could be read as another
// flag. A map name of `-host` must arrive as a map name. Values are checked before they are placed,
// and a rejected one is dropped rather than escaped, because there is no legitimate map called
// `-iwad` and a player who typed one is not going to be surprised to lose it.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_HOSTARGS_COMPUTE_H
#define ZX_HOSTARGS_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// What the player asked for, before it becomes a command line.
struct HostConfig
{
	std::string hostName;			// sv_hostname
	std::string iwad;				// bare filename
	std::vector<std::string> pwads;	// bare filenames, in load order
	std::string map;				// starting map lump
	// [rc4l] A catalogue entry's server.cfg, exec'd by the server so it can set its own gamemode,
	// map rotation and flags. A PATH rather than a bare filename, which is why it is checked with
	// IsSafeArgValue rather than IsBareFileName: the file lives in the entry's own folder and the
	// server has no reason to go looking for it anywhere else.
	std::string execCfg;

	// [rc4l] Further cfgs, exec'd right after the first so they win where they disagree: one per
	// chosen remix that has one, in group order. Each is a line or two meaning the same thing to
	// every experience it applies to.
	std::vector<std::string> execRemixCfgs;

	// [rc4l] Settings the gameplay panel decided, as name/value pairs, applied AFTER every exec so
	// they win over the cfgs.
	//
	// Set directly rather than written to a cfg and exec'd, which is what the lives control needs:
	// what it must set depends on the gamemode, and a shared file exec'd by a cooperative entry and
	// an invasion one cannot mean the right thing in both.
	std::vector<std::pair<std::string, std::string> > extraCvars;
	std::string password;			// empty for an open server
	std::string joinPassword;		// empty unless the operator wants a join gate
	std::string rconSecret;			// the one-shot secret the host authenticates with
	int gameMode;					// index into the mode table; < 0 means leave it alone
	int maxPlayers;
	int port;
	int parentPid;					// us, so the child can die with us; <= 0 to leave it out
	bool advertise;					// announce to the registry -- "global" hosting
	bool serveWads;					// hand our own files to joiners
	bool hideWindow;				// Windows: never show the server dialog

	HostConfig()
		: gameMode(-1), maxPlayers(8), port(0), parentPid(0), advertise(false), serveWads(true),
		  hideWindow(true) {}
};

// Whether a value may be placed on a command line as a VALUE. False for anything empty, anything
// starting with '-' or '+' (it would read as another flag), and anything carrying a control
// character, a quote or a backslash -- none of which belong in a map name or a WAD filename, and all
// of which are how a value stops being a value.
bool IsSafeArgValue(const std::string &value);

// Whether `name` is a bare filename we are willing to name on a command line: no directory
// separators, no `..`, no drive letters.
bool IsBareFileName(const std::string &name);

// [rc4l] Whether a resolved PATH may be named on a command line: everything IsSafeArgValue requires,
// plus no `..` anywhere in it.
//
// The WAD arguments take this rather than IsBareFileName, and the difference is a bug fix. A bare
// name makes the SERVER search for the file, using its own config -- which is not the one the client
// just wrote a download folder into. So a freshly downloaded pk3 was invisible to the server it had
// been fetched for: the server came up without it and the client that joined was told its lumps did
// not match. We have already resolved the file; handing over the path removes the second search and
// with it the chance of the two disagreeing.
bool IsSafeFilePath(const std::string &path);

// The full argv for a hosted server, `exePath` first. Unsafe values are dropped rather than escaped;
// see the header comment for why that is the kinder failure.
std::vector<std::string> BuildHostArgs(const std::string &exePath, const HostConfig &config);

// One argument, quoted for the Windows CreateProcess command-line grammar -- which is not the
// shell's. Backslashes are only special immediately before a quote, which is the rule everybody
// implements wrongly from memory and the reason this is a tested function rather than a lambda.
std::string QuoteWindowsArg(const std::string &arg);

// A whole argv joined into the single string CreateProcess demands.
std::string JoinWindowsCommandLine(const std::vector<std::string> &args);

// A port the player may host on. Rejects the privileged range outright: nothing below 1024 is
// bindable without elevation on any platform we ship to, and offering it only produces a failure
// the player cannot act on.
bool IsUsablePort(int port);

// The port to actually use, given what was asked for. 0 means "pick the default".
int ResolveHostPort(int requested, int defaultPort);

} // namespace zx

#endif // ZX_HOSTARGS_COMPUTE_H
