// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/web/computation/webargs_compute.h"

namespace zx { namespace web {

static int HexValue(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

std::string ComputeDecodeQueryValue(const std::string &value)
{
	std::string out;
	for (size_t i = 0; i < value.size(); ++i)
	{
		const char c = value[i];
		if (c == '+') { out += ' '; continue; }
		if (c == '%' && i + 2 < value.size())
		{
			const int hi = HexValue(value[i + 1]), lo = HexValue(value[i + 2]);
			if (hi >= 0 && lo >= 0)
			{
				out += (char)(hi * 16 + lo);
				i += 2;
				continue;
			}
		}
		out += c;
	}
	return out;
}

static std::vector<std::string> Split(const std::string &s, char sep)
{
	std::vector<std::string> parts;
	std::string cur;
	for (char c : s)
	{
		if (c == sep)
		{
			if (!cur.empty()) parts.push_back(cur);
			cur.clear();
		}
		else cur += c;
	}
	if (!cur.empty()) parts.push_back(cur);
	return parts;
}

std::vector<std::string> ComputeArgsFromQuery(const std::string &query)
{
	std::vector<std::string> args;
	const std::string q = (!query.empty() && query[0] == '?') ? query.substr(1) : query;
	for (const std::string &pair : Split(q, '&'))
	{
		const size_t eq = pair.find('=');
		if (eq == std::string::npos) continue;
		const std::string key = pair.substr(0, eq);
		const std::string value = ComputeDecodeQueryValue(pair.substr(eq + 1));
		if (value.empty()) continue;
		if (key == "iwad" || key == "skill" || key == "warp")
		{
			args.push_back("-" + key);
			for (const std::string &v : Split(value, ' ')) args.push_back(v);
		}
		else if (key == "file")
		{
			args.push_back("-file");
			for (const std::string &v : Split(value, ',')) args.push_back(v);
		}
		else if (key == "map")
		{
			args.push_back("+map");
			args.push_back(value);
		}
		else if (key == "args")
		{
			for (const std::string &v : Split(value, ' ')) args.push_back(v);
		}
	}
	return args;
}

}} // namespace zx::web
