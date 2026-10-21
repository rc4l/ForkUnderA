// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/json/computation/json_compute.h"

namespace zx { namespace json {

std::string ComputeJsonEscape(const std::string &text, JsonControls controls)
{
	static const char hex[] = "0123456789abcdef";
	std::string out;
	out.reserve(text.size());
	for (const char ch : text)
	{
		const unsigned char c = (unsigned char)ch;
		switch (c)
		{
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c >= 0x20) out += ch;
			else if (controls == JsonControls::Escape)
			{
				out += "\\u00";
				out += hex[c >> 4];
				out += hex[c & 0xf];
			}
		}
	}
	return out;
}

std::string ComputeJsonQuoted(const std::string &text, JsonControls controls)
{
	return "\"" + ComputeJsonEscape(text, controls) + "\"";
}

}} // namespace zx::json
