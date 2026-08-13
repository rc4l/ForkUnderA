// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/mcp-bridge/computation/mcprpc_compute.h"

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

void JsonEscape(const std::string &in, std::string &out)
{
	out.clear();
	for (size_t i = 0; i < in.size(); ++i)
	{
		unsigned char c = (unsigned char)in[i];
		switch (c)
		{
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n";  break;
			case '\r': break; // drop CR
			case '\t': out += "\\t";  break;
			default:
				if (c < 0x20)
				{
					char b[8];
					// hand-rolled \uXXXX (no sprintf) so this stays trivially pure/testable
					const char *hex = "0123456789abcdef";
					b[0] = '\\'; b[1] = 'u'; b[2] = '0'; b[3] = '0';
					b[4] = hex[(c >> 4) & 0xf]; b[5] = hex[c & 0xf]; b[6] = 0;
					out += b;
				}
				else out.push_back((char)c);
		}
	}
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
	std::string esc;
	JsonEscape(message, esc);
	std::string s = "{\"id\":";
	s += std::to_string(id);
	s += ",\"ok\":false,\"error\":\"";
	s += esc;
	s += "\"}";
	return s;
}

std::string BuildEvent(const std::string &name, const std::string &dataJson)
{
	std::string esc;
	JsonEscape(name, esc);
	std::string s = "{\"t\":\"event\",\"event\":\"";
	s += esc;
	s += "\",\"data\":";
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

}} // namespace zx::mcp
