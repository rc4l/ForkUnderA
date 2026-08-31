// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/continue/computation/continuerecord_compute.h"

#include <cstdlib>
#include <sstream>

namespace zx
{

namespace
{

const char *const kMagic = "fua-continue";

// One key and the rest of the line, because a WAD name may contain spaces and a password certainly
// may. Splitting on every space would corrupt exactly the fields that must survive verbatim.
bool SplitLine(const std::string &line, std::string &key, std::string &value)
{
	const std::string::size_type sp = line.find(' ');
	if (sp == std::string::npos)
	{
		key = line;
		value.clear();
		return key.empty() == false;
	}

	key = line.substr(0, sp);
	value = line.substr(sp + 1);
	return key.empty() == false;
}

// The WAD line is "name<tab>hash", tab-separated so a name with spaces still parses. A missing tab
// means a name and no hash, which is what a server that sent none gives us.
ContinueRecord::Wad SplitWad(const std::string &value)
{
	ContinueRecord::Wad out;

	const std::string::size_type first = value.find('\t');
	if (first == std::string::npos)
	{
		out.name = value;
		return out;
	}

	out.name = value.substr(0, first);

	const std::string::size_type second = value.find('\t', first + 1);
	if (second == std::string::npos)
	{
		out.hash = value.substr(first + 1);
		return out;
	}

	out.hash = value.substr(first + 1, second - first - 1);
	out.path = value.substr(second + 1);
	return out;
}

// Only ever asked about a kind there is something to write for; SerialiseContinue has already
// turned None away, so there is no third answer to give.
const char *KindName(ContinueKind kind)
{
	switch (kind)
	{
	case ContinueKind::Single: return "single";
	case ContinueKind::Hosted: return "hosted";
	default:                   return "server";
	}
}

} // namespace

std::string SerialiseContinue(const ContinueRecord &record)
{
	if (record.kind == ContinueKind::None)
		return std::string();

	std::ostringstream out;
	out << kMagic << ' ' << kContinueFormat << '\n';
	out << "kind " << KindName(record.kind) << '\n';
	out << "stamp " << record.stamp << '\n';

	if (record.kind == ContinueKind::Hosted)
	{
		const HostConfig &h = record.host;

		out << "host_name " << h.hostName << '\n';
		out << "host_iwad " << h.iwad << '\n';
		for (size_t i = 0; i < h.pwads.size(); ++i)
			out << "host_pwad " << h.pwads[i] << '\n';
		out << "host_map " << h.map << '\n';
		if (h.execCfg.empty() == false)
			out << "host_execcfg " << h.execCfg << '\n';
		for (size_t i = 0; i < h.execRemixCfgs.size(); ++i)
			out << "host_remixcfg " << h.execRemixCfgs[i] << '\n';
		// Name then value, split on the FIRST space: a cvar name never contains one and a value
		// frequently does.
		for (size_t i = 0; i < h.extraCvars.size(); ++i)
			out << "host_cvar " << h.extraCvars[i].first << ' ' << h.extraCvars[i].second << '\n';
		for (size_t i = 0; i < h.mapRotation.size(); ++i)
			out << "host_rotation " << h.mapRotation[i] << '\n';
		if (h.password.empty() == false)
			out << "host_password " << h.password << '\n';
		if (h.joinPassword.empty() == false)
			out << "host_joinpassword " << h.joinPassword << '\n';
		out << "host_gamemode " << h.gameMode << '\n';
		out << "host_maxplayers " << h.maxPlayers << '\n';
		out << "host_port " << h.port << '\n';
		out << "host_advertise " << (h.advertise ? 1 : 0) << '\n';
		out << "host_servewads " << (h.serveWads ? 1 : 0) << '\n';
		out << "host_hidewindow " << (h.hideWindow ? 1 : 0) << '\n';
	}
	else if (record.kind == ContinueKind::Single)
	{
		out << "save " << record.savePath << '\n';
		out << "savever " << record.saveVersion << '\n';
		if (record.mapName.empty() == false)
			out << "map " << record.mapName << '\n';
		if (record.mapWad.empty() == false)
			out << "mapwad " << record.mapWad << '\n';
	}
	else
	{
		out << "address " << record.address << '\n';
		if (record.password.empty() == false)
			out << "password " << record.password << '\n';
		if (record.serverName.empty() == false)
			out << "servername " << record.serverName << '\n';
	}

	if (record.iwad.empty() == false)
		out << "iwad " << record.iwad << '\n';
	if (record.iwadHash.empty() == false)
		out << "iwadhash " << record.iwadHash << '\n';

	// Tab-separated, and the path is simply absent when we do not have one -- a reader that stops
	// at two fields still gets a name and a digest, which is everything it had before.
	for (size_t i = 0; i < record.wads.size(); ++i)
	{
		out << "wad " << record.wads[i].name << '\t' << record.wads[i].hash;
		if (record.wads[i].path.empty() == false)
			out << '\t' << record.wads[i].path;
		out << '\n';
	}

	return out.str();
}

bool ParseContinue(const std::string &text, ContinueRecord &out)
{
	out = ContinueRecord();

	std::istringstream in(text);
	std::string line;

	if (!std::getline(in, line))
		return false;

	std::string key, value;
	if ((SplitLine(line, key, value) == false) || (key != kMagic))
		return false;

	// A record from a newer engine is refused, not read hopefully: a field whose meaning changed
	// would be read as absent here, and Continue would take the player somewhere plausible and wrong.
	const int format = atoi(value.c_str());
	if ((format <= 0) || (format > kContinueFormat))
		return false;

	while (std::getline(in, line))
	{
		if (line.empty())
			continue;

		if (SplitLine(line, key, value) == false)
			continue;

		if (key == "kind")
		{
			if (value == "single")        out.kind = ContinueKind::Single;
			else if (value == "server")   out.kind = ContinueKind::Server;
			else if (value == "hosted")   out.kind = ContinueKind::Hosted;
			else                        return false;	// a kind we do not know is not a kind we can act on
		}
		else if (key == "save")      out.savePath = value;
		else if (key == "savever")   out.saveVersion = atoi(value.c_str());
		else if (key == "stamp")     out.stamp = atoi(value.c_str());
		else if (key == "host_name")        out.host.hostName = value;
		else if (key == "host_iwad")        out.host.iwad = value;
		else if (key == "host_pwad")        out.host.pwads.push_back(value);
		else if (key == "host_map")         out.host.map = value;
		else if (key == "host_execcfg")     out.host.execCfg = value;
		else if (key == "host_remixcfg")    out.host.execRemixCfgs.push_back(value);
		else if (key == "host_rotation")    out.host.mapRotation.push_back(value);
		else if (key == "host_password")    out.host.password = value;
		else if (key == "host_joinpassword") out.host.joinPassword = value;
		else if (key == "host_gamemode")    out.host.gameMode = atoi(value.c_str());
		else if (key == "host_maxplayers")  out.host.maxPlayers = atoi(value.c_str());
		else if (key == "host_port")        out.host.port = atoi(value.c_str());
		else if (key == "host_advertise")   out.host.advertise = (atoi(value.c_str()) != 0);
		else if (key == "host_servewads")   out.host.serveWads = (atoi(value.c_str()) != 0);
		else if (key == "host_hidewindow")  out.host.hideWindow = (atoi(value.c_str()) != 0);
		else if (key == "host_cvar")
		{
			std::string cvarName, cvarValue;
			if (SplitLine(value, cvarName, cvarValue))
				out.host.extraCvars.push_back(std::make_pair(cvarName, cvarValue));
		}
		else if (key == "map")       out.mapName = value;
		else if (key == "mapwad")    out.mapWad = value;
		else if (key == "servername") out.serverName = value;
		else if (key == "address")   out.address = value;
		else if (key == "password")  out.password = value;
		else if (key == "iwad")      out.iwad = value;
		else if (key == "iwadhash")  out.iwadHash = value;
		else if (key == "wad")
		{
			const ContinueRecord::Wad wad = SplitWad(value);
			if (wad.name.empty() == false)
				out.wads.push_back(wad);
		}
		// Anything else is ignored, so a field added by a LATER build of the same format number
		// costs an older reader nothing.
	}

	// A kind missing the one field it cannot work without is not a record.
	if (out.kind == ContinueKind::Single)
		return out.savePath.empty() == false;
	if (out.kind == ContinueKind::Server)
		return out.address.empty() == false;
	// A rehost with no map is a server that would start on whatever the WADs default to, which is
	// not the game the player left.
	if (out.kind == ContinueKind::Hosted)
		return out.host.map.empty() == false;
	return false;
}

std::string ContinueDir(const std::string &configRoot, int instance)
{
	// The first instance keeps the plain name, matching the account keys: the folder a player finds
	// is the one the documentation names, however many copies they open afterwards.
	char suffix[16];
	suffix[0] = 0;
	if (instance > 0)
		snprintf(suffix, sizeof suffix, ".%d", instance + 1);

	if (configRoot.empty())
		return std::string("continue") + suffix;

	const char last = configRoot[configRoot.size() - 1];
	const bool bHasSeparator = (last == '/') || (last == '\\');

	return configRoot + (bHasSeparator ? "" : "/") + "continue" + suffix;
}

std::string ContinueOfflinePath(const std::string &configRoot, int instance)
{
	return ContinueDir(configRoot, instance) + "/offline.txt";
}

std::string ContinueServerPath(const std::string &configRoot, int instance)
{
	return ContinueDir(configRoot, instance) + "/server.txt";
}

std::string ContinueSavePath(const std::string &configRoot, int instance)
{
	return ContinueDir(configRoot, instance) + "/offline.zds";
}

} // namespace zx
