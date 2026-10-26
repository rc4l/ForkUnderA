// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/mcp-bridge/computation/mcprpc_compute.h"

#include <algorithm>
#include <cmath>
#include "features/json/computation/json_compute.h"

namespace zx { namespace mcp {

namespace {

// Find the value start for "key": in a flat object. Returns npos if the key isn't present.
size_t ValueStart(const std::string &obj, const char *key)
{
	std::string k = std::string("\"") + key + "\"";
	size_t p = obj.find(k);
	if (p == std::string::npos) return std::string::npos;
	size_t colon = obj.find(':', p + k.size());
	if (colon == std::string::npos) return std::string::npos;
	size_t i = colon + 1;
	while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t')) ++i;
	return i;
}

} // namespace

bool GetInt(const std::string &obj, const char *key, long &out)
{
	size_t i = ValueStart(obj, key);
	if (i == std::string::npos) return false;
	bool neg = false;
	if (i < obj.size() && obj[i] == '-') { neg = true; ++i; }
	if (i >= obj.size() || obj[i] < '0' || obj[i] > '9') return false;
	long v = 0;
	while (i < obj.size() && obj[i] >= '0' && obj[i] <= '9') { v = v * 10 + (obj[i] - '0'); ++i; }
	out = neg ? -v : v;
	return true;
}

// [rc4l] A flag, whether it was written as a JSON bool or as 1/0.
//
// GetInt reads digits and nothing else, so it returns false for `"on": false` -- and a caller that
// then leaves its default at "true" does the OPPOSITE of what was asked while reporting success.
// That is exactly how sim.pause came to answer {"on":false} by pausing.
bool GetBool(const std::string &obj, const char *key, bool &out)
{
	size_t i = ValueStart(obj, key);
	if (i == std::string::npos) return false;
	if (obj.compare(i, 4, "true") == 0)  { out = true;  return true; }
	if (obj.compare(i, 5, "false") == 0) { out = false; return true; }
	long v = 0;
	if (!GetInt(obj, key, v)) return false;
	out = (v != 0);
	return true;
}

bool GetFloat(const std::string &obj, const char *key, double &out)
{
	size_t i = ValueStart(obj, key);
	if (i == std::string::npos) return false;
	size_t start = i;
	bool neg = false;
	if (i < obj.size() && (obj[i] == '-' || obj[i] == '+')) { neg = (obj[i] == '-'); ++i; }
	double v = 0.0;
	bool anyDigit = false;
	while (i < obj.size() && obj[i] >= '0' && obj[i] <= '9') { v = v * 10.0 + (obj[i] - '0'); ++i; anyDigit = true; }
	if (i < obj.size() && obj[i] == '.')
	{
		++i;
		double scale = 0.1;
		while (i < obj.size() && obj[i] >= '0' && obj[i] <= '9') { v += (obj[i] - '0') * scale; scale *= 0.1; ++i; anyDigit = true; }
	}
	if (!anyDigit) { (void)start; return false; }
	out = neg ? -v : v;
	return true;
}

long DegreesToViewUnits(double deg)
{
	// G_AddViewAngle/G_AddViewPitch take an int that they shift <<16 onto a 2^32 BAM angle (full
	// circle). So 1 unit = 360/65536 deg, and units = deg * 65536/360. Exact for angles that divide
	// 360 evenly (135 -> 24576). Round to nearest so arbitrary degrees land on the closest BAM step.
	double u = deg * 65536.0 / 360.0;
	return static_cast<long>(u < 0 ? u - 0.5 : u + 0.5);
}

bool GetStr(const std::string &obj, const char *key, std::string &out)
{
	size_t i = ValueStart(obj, key);
	if (i == std::string::npos || i >= obj.size() || obj[i] != '"') return false;
	out.clear();
	for (size_t j = i + 1; j < obj.size(); ++j)
	{
		char c = obj[j];
		if (c == '\\' && j + 1 < obj.size())
		{
			char n = obj[++j];
			switch (n)
			{
				case 'n':  out.push_back('\n'); break;
				case 't':  out.push_back('\t'); break;
				case 'r':  out.push_back('\r'); break;
				case '"':  out.push_back('"');  break;
				case '\\': out.push_back('\\'); break;
				case '/':  out.push_back('/');  break;
				default:   out.push_back(n);    break;
			}
		}
		else if (c == '"')
			return true;
		else
			out.push_back(c);
	}
	return false; // unterminated string
}

bool ComputeHoldCommand(HeldCommands &held, const std::string &text, bool startingUp)
{
	if (!startingUp && held.waiting.empty()) return false;
	held.waiting.push_back(text);
	return true;
}

std::vector<std::string> ComputeReleaseCommands(HeldCommands &held, bool startingUp)
{
	std::vector<std::string> ready;
	if (!startingUp) ready.swap(held.waiting);
	return ready;
}

RpcRequest ParseRequest(const std::string &line)
{
	RpcRequest r;
	r.valid = false;
	r.id = -1;
	r.args = "{}";
	if (!GetStr(line, "cmd", r.cmd) || r.cmd.empty())
		return r;
	GetInt(line, "id", r.id);
	// Extract the raw "args" object substring by brace-matching, so nested objects survive intact.
	std::string k = "\"args\"";
	size_t p = line.find(k);
	if (p != std::string::npos)
	{
		size_t brace = line.find('{', p + k.size());
		if (brace != std::string::npos)
		{
			int depth = 0;
			bool inStr = false;
			for (size_t i = brace; i < line.size(); ++i)
			{
				char c = line[i];
				if (inStr)
				{
					if (c == '\\') { ++i; continue; }
					if (c == '"') inStr = false;
				}
				else if (c == '"') inStr = true;
				else if (c == '{') ++depth;
				else if (c == '}')
				{
					if (--depth == 0) { r.args = line.substr(brace, i - brace + 1); break; }
				}
			}
		}
	}
	r.valid = true;
	return r;
}

std::string BuildOkResponse(long id, const std::string &bodyJson)
{
	std::string s = "{\"id\":";
	s += std::to_string(id);
	s += ",\"ok\":true,\"result\":";
	s += bodyJson;
	s += "}";
	return s;
}

std::string BuildErrResponse(long id, const std::string &message)
{
	std::string s = "{\"id\":";
	s += std::to_string(id);
	s += ",\"ok\":false,\"error\":";
	s += zx::json::ComputeJsonQuoted(message);
	s += "}";
	return s;
}

std::string BuildEvent(const std::string &name, const std::string &dataJson)
{
	std::string s = "{\"t\":\"event\",\"event\":";
	s += zx::json::ComputeJsonQuoted(name);
	s += ",\"data\":";
	s += dataJson.empty() ? std::string("{}") : dataJson;
	s += "}";
	return s;
}

// FNV-1a/64
uint64_t FnvInit()
{
	return 1469598103934665603ULL;
}

uint64_t FnvMixU64(uint64_t h, uint64_t v)
{
	for (int i = 0; i < 8; ++i)
	{
		h ^= (uint64_t)(v & 0xff);
		h *= 1099511628211ULL;
		v >>= 8;
	}
	return h;
}

uint64_t FnvMixStr(uint64_t h, const std::string &s)
{
	for (size_t i = 0; i < s.size(); ++i)
	{
		h ^= (uint64_t)(unsigned char)s[i];
		h *= 1099511628211ULL;
	}
	return h;
}

long StepTarget(long currentLevelTime, int tics)
{
	if (tics < 1) tics = 1;
	return currentLevelTime + tics;
}

bool StepComplete(long currentLevelTime, long targetLevelTime)
{
	return currentLevelTime >= targetLevelTime;
}

namespace {
	std::string Fixed3(double v)
	{
		if (std::isnan(v) || std::isinf(v)) return "0";
		bool neg = v < 0; if (neg) v = -v;
		long long scaled = (long long)(v * 1000.0 + 0.5);
		std::string whole = std::to_string(scaled / 1000);
		long long frac = scaled % 1000;
		std::string f = std::to_string(frac);
		while (f.size() < 3) f = "0" + f;
		return (neg ? "-" : "") + whole + "." + f;
	}
}

std::string PerfSummaryJson(const zx::perfstats::PerfSummary &s)
{
	std::string j = "{\"n\":" + std::to_string(s.n);
	j += ",\"mean_ms\":" + Fixed3(s.mean);
	j += ",\"min_ms\":" + Fixed3(s.min);
	j += ",\"max_ms\":" + Fixed3(s.max);
	j += ",\"p50_ms\":" + Fixed3(s.p50);
	j += ",\"p95_ms\":" + Fixed3(s.p95);
	j += ",\"p99_ms\":" + Fixed3(s.p99);
	j += ",\"fps_avg\":" + Fixed3(s.fpsAvg);
	j += ",\"fps_1pct_low\":" + Fixed3(s.fps1pctLow) + "}";
	return j;
}

}} // namespace zx::mcp
