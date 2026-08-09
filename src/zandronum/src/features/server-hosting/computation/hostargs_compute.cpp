// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-hosting/computation/hostargs_compute.h"

#include <cstdio>

namespace zx
{

namespace
{
// Anything below this needs elevation to bind on every platform we ship to.
const int kFirstUnprivilegedPort = 1024;
const int kLastPort = 65535;

std::string IntToString(int value)
{
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%d", value);
	return std::string(buffer);
}

// Placed only if it survives IsSafeArgValue. A dropped flag is a server that differs from what was
// asked for; a smuggled one is a server that does something else entirely.
void PushOption(std::vector<std::string> &out, const char *flag, const std::string &value)
{
	if (!IsSafeArgValue(value))
		return;

	out.push_back(flag);
	out.push_back(value);
}
} // namespace

bool IsSafeArgValue(const std::string &value)
{
	if (value.empty())
		return false;

	// A leading '-' or '+' makes the value indistinguishable from the next flag, and the engine's
	// argument parser reads position, not intent.
	if ((value[0] == '-') || (value[0] == '+'))
		return false;

	for (size_t i = 0; i < value.size(); ++i)
	{
		const unsigned char c = static_cast<unsigned char>(value[i]);

		// Control characters, including the newline that would end one line of a config file and
		// begin another.
		if (c < 0x20 || c == 0x7f)
			return false;

		// Quotes and backslashes are how a value escapes its own argument on Windows. Nothing that
		// belongs in a map name or a WAD filename needs them.
		if (c == '"' || c == '\\')
			return false;
	}

	return true;
}

bool IsSafeFilePath(const std::string &path)
{
	// [rc4l] Everything IsSafeArgValue refuses, plus traversal. A path we hand the server is one WE
	// resolved off the filesystem, so it is not attacker-chosen -- but the filename inside it came
	// out of a catalogue json, and ".." in an argument is never something we meant to write.
	if (!IsSafeArgValue(path))
		return false;

	if (path.find("..") != std::string::npos)
		return false;

	return true;
}

bool IsBareFileName(const std::string &name)
{
	if (!IsSafeArgValue(name))
		return false;

	if (name.find('/') != std::string::npos)
		return false;
	if (name.find(':') != std::string::npos)		// drive letters, and NTFS alternate streams
		return false;
	if (name == "." || name == "..")
		return false;
	if (name.find("..") != std::string::npos)
		return false;

	return true;
}

std::vector<std::string> BuildHostArgs(const std::string &exePath, const HostConfig &config)
{
	std::vector<std::string> out;
	out.push_back(exePath);

	// -host makes it a server. Note what is NOT here: -stdout. That flag asks the engine to find
	// somewhere to write, and when its probe fails -- which it does on an anonymous pipe -- it calls
	// AllocConsole and a console window appears on the desktop of a player who asked for a headless
	// server. The child writes to its inherited handle directly instead; see HostChildEcho.
	out.push_back("-host");

	// [rc4l] Windows only in effect, but always passed: the server dialog is created by the child
	// itself, so nothing we can set at spawn time suppresses it. A hidden window also has no taskbar
	// button, which is the actual requirement.
	if (config.hideWindow)
		out.push_back("-fua_hidden");

	// Who to die with. The kernel-level guarantees (job object, PR_SET_PDEATHSIG) are the primary
	// mechanism where they exist; this is what covers macOS, which has neither.
	if (config.parentPid > 0)
	{
		out.push_back("-fua_hostparent");
		out.push_back(IntToString(config.parentPid));
	}

	// [rc4l] PATHS, not just bare names, and that is the fix rather than a loosening.
	//
	// A bare name makes the SERVER go and find the file, and it searches its own config -- which is
	// not the one the client just wrote. Download an experience and host it and the freshly fetched
	// pk3 sat in a folder registered in the client's FileSearch.Directories and nowhere the server
	// looked: it skipped the file, came up with one PWAD instead of two, and the client that joined
	// it was told its lumps did not match. Two processes searching separately for the same file is
	// the whole bug; handing over what we already resolved removes the second search.
	if (IsSafeFilePath(config.iwad))
	{
		out.push_back("-iwad");
		out.push_back(config.iwad);
	}

	for (size_t i = 0; i < config.pwads.size(); ++i)
	{
		if (!IsSafeFilePath(config.pwads[i]))
			continue;

		out.push_back("-file");
		out.push_back(config.pwads[i]);
	}

	// [rc4l] THE FIRST '+' ARGUMENT, AND IT HAS TO STAY THAT WAY.
	//
	// The engine applies these left to right, so whatever comes last wins. Putting the entry's cfg
	// ahead of every setting means the SETTINGS MENU BEATS THE EXPERIENCE, always: an entry describes
	// what to play, and the host decides how to run it. A cfg full of addmap lines would otherwise
	// choose where the host lands, and a cfg naming a player limit would quietly overrule the number
	// the host just typed into the form.
	//
	// It is stated as "first '+'" rather than as a list of the settings it must precede, because the
	// list grows. A new +setting appended anywhere below is after this by construction and therefore
	// wins for free. TheSettingsMenuBeatsTheExperienceConfig in the tests pins exactly that.
	if (IsSafeArgValue(config.execCfg))
	{
		out.push_back("+exec");
		out.push_back(config.execCfg);
	}

	if (IsSafeArgValue(config.map))
	{
		out.push_back("+map");
		out.push_back(config.map);
	}

	out.push_back("-port");
	out.push_back(IntToString(ResolveHostPort(config.port, 10666)));

	// The name is the one field that is genuinely free text, and the one most likely to arrive with
	// something odd in it. PushOption drops it rather than mangling it.
	PushOption(out, "+sv_hostname", config.hostName);

	if (config.gameMode >= 0)
	{
		out.push_back("+gamemode");
		out.push_back(IntToString(config.gameMode));
	}

	out.push_back("+sv_maxclients");
	out.push_back(IntToString(config.maxPlayers));
	out.push_back("+sv_maxplayers");
	out.push_back(IntToString(config.maxPlayers));

	// A password only counts if it is also enforced. Setting one without the flag produces a server
	// that looks locked in the browser and lets anybody in.
	if (IsSafeArgValue(config.password))
	{
		out.push_back("+sv_password");
		out.push_back(config.password);
		out.push_back("+sv_forcepassword");
		out.push_back("1");
	}

	if (IsSafeArgValue(config.joinPassword))
	{
		out.push_back("+sv_joinpassword");
		out.push_back(config.joinPassword);
		out.push_back("+sv_forcejoinpassword");
		out.push_back("1");
	}

	// [rc4l] The secret that makes the player who started this server its administrator. Generated
	// per spawn and never reused, so it is worth no more than the lifetime of the process it
	// belongs to.
	PushOption(out, "+sv_rconpassword", config.rconSecret);

	// [rc4l] sv_fua_serverregistry_announce, NOT the old +sv_updatemaster: that cvar no longer
	// exists in this fork, so the child silently ignored it and fell back to the announce default
	// (on) -- meaning a "local only" host still announced itself to the public registry, and this
	// toggle did nothing at all.
	out.push_back("+sv_fua_serverregistry_announce");
	out.push_back(config.advertise ? "1" : "0");

	out.push_back("+sv_fua_download");
	out.push_back(config.serveWads ? "1" : "0");

	return out;
}

std::string QuoteWindowsArg(const std::string &arg)
{
	// Only quote when we have to. An unquoted argument survives verbatim and is easier to read in
	// a process listing, which is where anyone debugging this will be looking.
	bool needsQuotes = arg.empty();
	for (size_t i = 0; i < arg.size() && !needsQuotes; ++i)
	{
		const char c = arg[i];
		if (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '"')
			needsQuotes = true;
	}

	if (!needsQuotes)
		return arg;

	std::string out;
	out += '"';

	for (size_t i = 0; i < arg.size(); ++i)
	{
		// [rc4l] Backslashes are ONLY special immediately before a quote. Doubling them everywhere
		// is the mistake everyone makes from memory, and it turns C:\wads\ into C:\\wads\\ in the
		// child's argv.
		size_t backslashes = 0;
		while ((i < arg.size()) && (arg[i] == '\\'))
		{
			++backslashes;
			++i;
		}

		if (i == arg.size())
		{
			// Run of backslashes at the very end: they precede the closing quote we are about to
			// add, so they do need doubling.
			out.append(backslashes * 2, '\\');
			break;
		}

		if (arg[i] == '"')
		{
			out.append(backslashes * 2 + 1, '\\');
			out += '"';
		}
		else
		{
			out.append(backslashes, '\\');
			out += arg[i];
		}
	}

	out += '"';
	return out;
}

std::string JoinWindowsCommandLine(const std::vector<std::string> &args)
{
	std::string out;

	for (size_t i = 0; i < args.size(); ++i)
	{
		if (i > 0)
			out += ' ';
		out += QuoteWindowsArg(args[i]);
	}

	return out;
}

bool IsUsablePort(int port)
{
	return (port >= kFirstUnprivilegedPort) && (port <= kLastPort);
}

int ResolveHostPort(int requested, int defaultPort)
{
	if (IsUsablePort(requested))
		return requested;

	// A default that is itself unusable is a programming error, not a player one -- but returning a
	// privileged port would produce a bind failure blamed on the player, so fall back to something
	// that can at least be tried.
	return IsUsablePort(defaultPort) ? defaultPort : 10666;
}

} // namespace zx
