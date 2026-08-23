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
void SplitWad(const std::string &value, std::string &name, std::string &hash)
{
	const std::string::size_type tab = value.find('\t');
	if (tab == std::string::npos)
	{
		name = value;
		hash.clear();
		return;
	}

	name = value.substr(0, tab);
	hash = value.substr(tab + 1);
}

// Only ever asked about a kind there is something to write for; SerialiseContinue has already
// turned None away, so there is no third answer to give.
const char *KindName(ContinueKind kind)
{
	return (kind == ContinueKind::Single) ? "single" : "server";
}

} // namespace

std::string SerialiseContinue(const ContinueRecord &record)
{
	if (record.kind == ContinueKind::None)
		return std::string();

	std::ostringstream out;
	out << kMagic << ' ' << kContinueFormat << '\n';
	out << "kind " << KindName(record.kind) << '\n';

	if (record.kind == ContinueKind::Single)
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

	for (size_t i = 0; i < record.wads.size(); ++i)
		out << "wad " << record.wads[i].first << '\t' << record.wads[i].second << '\n';

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
			if (value == "single")      out.kind = ContinueKind::Single;
			else if (value == "server") out.kind = ContinueKind::Server;
			else                        return false;	// a kind we do not know is not a kind we can act on
		}
		else if (key == "save")      out.savePath = value;
		else if (key == "savever")   out.saveVersion = atoi(value.c_str());
		else if (key == "map")       out.mapName = value;
		else if (key == "mapwad")    out.mapWad = value;
		else if (key == "servername") out.serverName = value;
		else if (key == "address")   out.address = value;
		else if (key == "password")  out.password = value;
		else if (key == "iwad")      out.iwad = value;
		else if (key == "iwadhash")  out.iwadHash = value;
		else if (key == "wad")
		{
			std::string name, hash;
			SplitWad(value, name, hash);
			if (name.empty() == false)
				out.wads.push_back(std::make_pair(name, hash));
		}
		// Anything else is ignored, so a field added by a LATER build of the same format number
		// costs an older reader nothing.
	}

	// A kind missing the one field it cannot work without is not a record.
	if (out.kind == ContinueKind::Single)
		return out.savePath.empty() == false;
	if (out.kind == ContinueKind::Server)
		return out.address.empty() == false;

	return false;
}

std::string ContinueDir(const std::string &configRoot)
{
	if (configRoot.empty())
		return std::string("continue");

	const char last = configRoot[configRoot.size() - 1];
	const bool bHasSeparator = (last == '/') || (last == '\\');

	return configRoot + (bHasSeparator ? "" : "/") + "continue";
}

std::string ContinueRecordPath(const std::string &configRoot)
{
	return ContinueDir(configRoot) + "/session.txt";
}

std::string ContinueSavePath(const std::string &configRoot)
{
	return ContinueDir(configRoot) + "/session.zds";
}

} // namespace zx
