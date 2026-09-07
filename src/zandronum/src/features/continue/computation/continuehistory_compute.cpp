// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/continue/computation/continuehistory_compute.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace zx
{

namespace
{

const char *const kHistoryMagic = "fua-continue-history";

// The line that starts an entry. A whole line of its own so a value can never be mistaken for one:
// every other line in the file has a key and a space, and this has neither.
const char *const kEntryMarker = "entry";

// [rc4l] A file, named the way a comparison should see it: the last component, lowercased.
//
// The same file arrives spelled differently depending on who wrote the record down. A remembered
// host config holds "doom2.wad" because that is what the player picked; the config a RUNNING server
// reports holds the absolute path the engine resolved it to. Comparing those as strings makes one
// game into two rows, and then the row you are standing in is not the row you came from -- which is
// how rehosting a game added a second copy of it AND left the pill offering to take you back to the
// game you were already inside.
std::string BaseName(const std::string &path)
{
	const std::string::size_type slash = path.find_last_of("/\\");
	return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

std::string Lowered(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i)
	{
		const char c = s[i];
		out.push_back((c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c);
	}
	return out;
}

// [rc4l] The files a session was played with, as one comparable string. Names only, lowercased:
// a hash would make the same mod at a different version a different row, and re-downloading a pack
// is not a different thing to have been doing.
std::string WadSetKey(const ContinueRecord &record)
{
	std::string out = Lowered(BaseName(record.iwad));
	for (size_t i = 0; i < record.wads.size(); ++i)
		out += "\n" + Lowered(BaseName(record.wads[i].name));
	return out;
}

// One count and its unit, singular when it is one of them.
std::string Ago(long long count, const char *unit)
{
	char buf[64];
	snprintf(buf, sizeof buf, "%lld %s%s ago", count, unit, (count == 1) ? "" : "s");
	return buf;
}

const long long kMinute = 60;
const long long kHour = 60 * kMinute;
const long long kDay = 24 * kHour;
const long long kWeek = 7 * kDay;
const long long kYear = 365 * kDay;

} // namespace

int ClampContinueHistoryLimit(int requested)
{
	if (requested < kContinueHistoryMin)
		return kContinueHistoryMin;
	if (requested > kContinueHistoryMax)
		return kContinueHistoryMax;
	return requested;
}

std::string ContinueIdentity(const ContinueRecord &record)
{
	switch (record.kind)
	{
	case ContinueKind::Server:
		// The address, and nothing else. A server that renames itself is the same server, and one
		// that reloads its WADs is still the place the player goes in the evening.
		return "server\n" + Lowered(record.address);

	case ContinueKind::Single:
		return "single\n" + Lowered(record.mapName) + "\n" + WadSetKey(record);

	case ContinueKind::Hosted:
		break;

	default:
		return std::string();
	}

	// What would START it again, which is what the row leads to: the same map on the same files in
	// the same mode. Two hosted rows differing only in a password are the same game to everyone who
	// plays it.
	std::ostringstream out;
	out << "hosted\n" << Lowered(record.host.map) << "\n" << Lowered(BaseName(record.host.iwad));
	for (size_t i = 0; i < record.host.pwads.size(); ++i)
		out << "\n" << Lowered(BaseName(record.host.pwads[i]));
	out << "\n" << record.host.gameMode;
	return out.str();
}

std::string ContinueEntryLabel(const ContinueRecord &record)
{
	switch (record.kind)
	{
	case ContinueKind::Server:
		// The name if the browser ever told us one, the address if it did not. An address is honest
		// and unreadable, so it is the fallback rather than the answer.
		return record.serverName.empty() ? record.address : record.serverName;

	case ContinueKind::Hosted:
		return "Hosting " + record.host.map;

	case ContinueKind::Single:
		// Which megawad, because MAP01 on its own does not identify anything once a player has more
		// than one of them.
		return record.mapWad.empty() ? record.mapName : (record.mapName + " in " + record.mapWad);

	default:
		return std::string();
	}
}

std::string FormatLastPlayed(long long nowEpoch, long long thenEpoch)
{
	// Written before the clock field existed, so there is nothing to say. A dash rather than a
	// guess: an entry from an older build is not from 1970.
	if (thenEpoch <= 0)
		return "-";

	const long long delta = nowEpoch - thenEpoch;

	// [rc4l] A record from the future, which is what a machine whose clock has since been corrected
	// backwards leaves behind. It is still the thing they last played, so it is still at the top of
	// the list; only the age is unsayable.
	if (delta < 0)
		return "just now";

	if (delta < kMinute)
		return "just now";
	if (delta < kHour)
		return Ago(delta / kMinute, "min");
	if (delta < kDay)
		return Ago(delta / kHour, "hour");
	if (delta < kWeek)
		return Ago(delta / kDay, "day");
	if (delta < kYear)
		return Ago(delta / kWeek, "week");

	return Ago(delta / kYear, "year");
}

const ContinueRecord *FindContinueEntry(const std::vector<ContinueRecord> &history,
	const std::string &identity)
{
	if (identity.empty())
		return NULL;

	for (size_t i = 0; i < history.size(); ++i)
	{
		if (ContinueIdentity(history[i]) == identity)
			return &history[i];
	}
	return NULL;
}

int NextContinueStamp(const std::vector<ContinueRecord> &history)
{
	int highest = 0;
	for (size_t i = 0; i < history.size(); ++i)
	{
		if (history[i].stamp > highest)
			highest = history[i].stamp;
	}
	return highest + 1;
}

std::vector<ContinueRecord> TrimContinueHistory(const std::vector<ContinueRecord> &history, int limit)
{
	const int cap = ClampContinueHistoryLimit(limit);

	// Selection by highest stamp rather than std::sort, so entries that somehow share a stamp keep
	// the order the file gave them. A stable answer matters more here than a fast one: the list is
	// fifty rows at the very most, and a list that shuffles between two reads of the same file is a
	// list the player cannot point at.
	std::vector<ContinueRecord> out;
	std::vector<bool> taken(history.size(), false);

	while (static_cast<int>(out.size()) < cap)
	{
		size_t best = history.size();
		for (size_t i = 0; i < history.size(); ++i)
		{
			if (taken[i])
				continue;
			if ((best == history.size()) || (history[i].stamp > history[best].stamp))
				best = i;
		}

		if (best == history.size())
			break;					// nothing left to take

		taken[best] = true;
		out.push_back(history[best]);
	}

	return out;
}

std::vector<ContinueRecord> InsertContinueEntry(const std::vector<ContinueRecord> &history,
	const ContinueRecord &entry, int limit)
{
	const std::string identity = ContinueIdentity(entry);
	if (identity.empty())
		return history;				// nothing to continue is nothing to remember

	std::vector<ContinueRecord> merged;
	merged.reserve(history.size() + 1);
	merged.push_back(entry);

	// The same thing done again is the same row, moved to the top and rewritten -- not a second row
	// saying what the first one said.
	for (size_t i = 0; i < history.size(); ++i)
	{
		if (ContinueIdentity(history[i]) != identity)
			merged.push_back(history[i]);
	}

	// The entry has to outrank what it is joining, or a replacement would sort back underneath the
	// row it replaced. Asked here rather than trusted from the caller: the caller's stamp came from
	// a file read that may be older than this list.
	const int highest = NextContinueStamp(history) - 1;
	if (merged[0].stamp <= highest)
		merged[0].stamp = highest + 1;

	return TrimContinueHistory(merged, limit);
}

std::vector<ContinueRecord> RemoveContinueEntry(const std::vector<ContinueRecord> &history, int index)
{
	if ((index < 0) || (index >= static_cast<int>(history.size())))
		return history;

	std::vector<ContinueRecord> out;
	out.reserve(history.size() - 1);
	for (size_t i = 0; i < history.size(); ++i)
	{
		if (static_cast<int>(i) != index)
			out.push_back(history[i]);
	}
	return out;
}

std::string SerialiseContinueHistory(const std::vector<ContinueRecord> &history)
{
	std::ostringstream out;
	out << kHistoryMagic << ' ' << kContinueHistoryFormat << '\n';

	for (size_t i = 0; i < history.size(); ++i)
	{
		const std::string body = SerialiseContinueBody(history[i]);
		if (body.empty())
			continue;				// a record with no kind is not written out as an empty entry

		out << kEntryMarker << '\n' << body;
	}

	// [rc4l] The header is written even for an empty list, so "nothing to continue" is something the
	// file can SAY. Without a file, load falls back to migrating the old two records -- and a player
	// who cleared their history would find it back on the next launch.
	return out.str();
}

bool ParseContinueHistory(const std::string &text, std::vector<ContinueRecord> &out)
{
	out.clear();

	std::istringstream in(text);
	std::string line;

	if (!std::getline(in, line))
		return false;

	{
		const std::string::size_type sp = line.find(' ');
		if (sp == std::string::npos)
			return false;

		if (line.substr(0, sp) != kHistoryMagic)
			return false;

		// Refused rather than read hopefully, the same way one record is: a list written by a build
		// that knows a field we do not is a list we would describe wrongly.
		const int format = atoi(line.c_str() + sp + 1);
		if ((format <= 0) || (format > kContinueHistoryFormat))
			return false;
	}

	// Split on the marker and hand each block to the record parser. Anything before the first marker
	// is not an entry and is dropped.
	std::string body;
	bool bInEntry = false;

	while (std::getline(in, line))
	{
		if (line == kEntryMarker)
		{
			if (bInEntry)
			{
				ContinueRecord record;
				if (ParseContinueBody(body, record))
					out.push_back(record);	// and a block that does not parse costs only itself
			}

			body.clear();
			bInEntry = true;
			continue;
		}

		if (bInEntry)
			body += line + "\n";
	}

	if (bInEntry)
	{
		ContinueRecord record;
		if (ParseContinueBody(body, record))
			out.push_back(record);
	}

	return true;
}

} // namespace zx
