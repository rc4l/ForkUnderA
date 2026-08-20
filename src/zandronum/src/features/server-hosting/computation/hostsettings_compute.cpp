// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "hostsettings_compute.h"

namespace zx
{

namespace
{

// [rc4l] The passwords, named rather than pattern-matched: a rule like "anything ending in
// password" would also catch a setting somebody adds later that genuinely belongs in the folder,
// and the failure would be silent in the direction that loses data.
const char *const kSessionNames[] =
{
	"sv_password",
	"sv_joinpassword",
	"sv_rconpassword",
};

bool HasPrefix(const std::string &s, const char *prefix)
{
	std::string::size_type i = 0;
	for (; prefix[i] != '\0'; ++i)
	{
		if ((i >= s.size()) || (s[i] != prefix[i]))
			return false;
	}

	return true;
}

} // namespace

SettingScope ComputeSettingScope(const std::string &name)
{
	for (std::size_t i = 0; i < sizeof(kSessionNames) / sizeof(kSessionNames[0]); ++i)
	{
		if (name == kSessionNames[i])
			return SettingScope::Session;
	}

	// [rc4l] Every client cvar, which is the whole of what this machine answers for itself -- the
	// preferred port among them.
	if (HasPrefix(name, "cl_"))
		return SettingScope::Machine;

	return SettingScope::Preset;
}

std::vector<std::pair<std::string, std::string> > ComputeSavedCvars(
	const std::vector<std::pair<std::string, std::string> > &cvars)
{
	std::vector<std::pair<std::string, std::string> > out;

	for (std::size_t i = 0; i < cvars.size(); ++i)
	{
		if (ComputeSettingScope(cvars[i].first) == SettingScope::Preset)
			out.push_back(cvars[i]);
	}

	return out;
}

int ComputeClampedMaxPlayers(int wanted)
{
	// [rc4l] MAXPLAYERS is 64 in this engine; the floor is one because a server with room for
	// nobody is not a server.
	if (wanted < 1)
		return 1;

	if (wanted > 64)
		return 64;

	return wanted;
}

const char *const kFuaDefaultBuildServerName = "FUA Custom Server";

std::string ComputeHostDisplayName(const std::string &typed)
{
	// [rc4l] Whitespace alone is an empty name that looks like one, so it is treated as the empty
	// case rather than allowed through to become a server called "   ".
	for (std::size_t i = 0; i < typed.size(); ++i)
	{
		const char c = typed[i];
		if ((c != ' ') && (c != '\t'))
			return typed;
	}

	return std::string(kFuaDefaultBuildServerName);
}

} // namespace zx
