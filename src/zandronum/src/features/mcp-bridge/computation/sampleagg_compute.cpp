// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See sampleagg_compute.h for why the counting lives away from the sampling.

#include "features/mcp-bridge/computation/sampleagg_compute.h"

#include <algorithm>
#include <cstdio>
#include <map>

namespace zx
{

namespace
{

// A function is identified by its name AND the module it came from: two DLLs can each export an
// `init`, and folding them together would invent a hot function that does not exist.
typedef std::pair<std::string, std::string> FuncKey;

std::string LowerCopy(const std::string &s)
{
	std::string out = s;
	for (std::size_t i = 0; i < out.size(); ++i)
	{
		if ((out[i] >= 'A') && (out[i] <= 'Z'))
			out[i] = static_cast<char>(out[i] - 'A' + 'a');
	}

	return out;
}

// A JSON string. A C++ symbol carries template arguments and operator names, so quotes and
// backslashes have to survive the trip; nothing else in a symbol needs escaping.
std::string JsonString(const std::string &value)
{
	std::string out = "\"";

	for (std::size_t i = 0; i < value.size(); ++i)
	{
		const char c = value[i];

		if ((c == '"') || (c == '\\'))
			out += '\\';

		// A control character cannot appear in a symbol or a module name, and would make the report
		// unparseable if it somehow did.
		if (static_cast<unsigned char>(c) >= 32)
			out += c;
	}

	return out + "\"";
}

std::string Fixed(double value, int places)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "%.*f", places, value);

	return std::string(buf);
}

std::string U(unsigned long long value)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%llu", value);

	return std::string(buf);
}

bool Hotter(const SampleFunc &a, const SampleFunc &b)
{
	if (a.count != b.count)
		return a.count > b.count;

	// Same count: name order, so the table is stable run to run and can be diffed.
	if (a.symbol != b.symbol)
		return a.symbol < b.symbol;

	return a.dso < b.dso;
}

} // namespace

SampleHit::SampleHit()
{
}

SampleHit::SampleHit(const std::string &symbol, const std::string &dso)
	: symbol(symbol), dso(dso)
{
}

SampleFunc::SampleFunc()
	: count(0), percent(0.0)
{
}

SampleFunc::SampleFunc(const std::string &symbol, const std::string &dso, unsigned long long count,
	double percent)
	: symbol(symbol), dso(dso), count(count), percent(percent)
{
}

std::vector<SampleFunc> RankSamples(const std::vector<SampleHit> &hits, std::size_t top)
{
	std::map<FuncKey, unsigned long long> counts;

	for (std::size_t i = 0; i < hits.size(); ++i)
	{
		// An address the symboliser could not name is still a sample that happened. It is counted
		// under a placeholder rather than dropped, because dropping it would hand its share to
		// everything else and overstate whatever we DID resolve.
		const std::string symbol = hits[i].symbol.empty() ? std::string("[unknown]") : hits[i].symbol;

		counts[FuncKey(symbol, hits[i].dso)]++;
	}

	const unsigned long long total = static_cast<unsigned long long>(hits.size());

	std::vector<SampleFunc> out;
	out.reserve(counts.size());

	for (std::map<FuncKey, unsigned long long>::const_iterator it = counts.begin();
		it != counts.end(); ++it)
	{
		// Of the whole run. Guarded because a caller may rank nothing at all, and a profile is not
		// worth a division by zero.
		const double percent = (total > 0)
			? ((static_cast<double>(it->second) * 100.0) / static_cast<double>(total))
			: 0.0;

		out.push_back(SampleFunc(it->first.first, it->first.second, it->second, percent));
	}

	std::sort(out.begin(), out.end(), Hotter);

	if ((top > 0) && (out.size() > top))
		out.resize(top);

	return out;
}

std::vector<SampleHit> OnlyFrom(const std::vector<SampleHit> &hits, const std::string &dso)
{
	const std::string want = LowerCopy(dso);

	std::vector<SampleHit> out;

	for (std::size_t i = 0; i < hits.size(); ++i)
	{
		if (LowerCopy(hits[i].dso) == want)
			out.push_back(hits[i]);
	}

	return out;
}

std::string SampleReportJson(const std::vector<SampleFunc> &funcs, double seconds,
	unsigned long long total)
{
	std::string out = "{\"available\":true,\"backend\":\"bridge\",\"seconds\":";
	out += Fixed(seconds, 3);
	out += ",\"samples\":" + U(total);
	out += ",\"functions\":[";

	for (std::size_t i = 0; i < funcs.size(); ++i)
	{
		if (i > 0)
			out += ",";

		out += "{\"symbol\":" + JsonString(funcs[i].symbol);
		out += ",\"dso\":" + JsonString(funcs[i].dso);
		out += ",\"samples\":" + U(funcs[i].count);
		out += ",\"percent\":" + Fixed(funcs[i].percent, 2) + "}";
	}

	return out + "]}";
}

} // namespace zx
