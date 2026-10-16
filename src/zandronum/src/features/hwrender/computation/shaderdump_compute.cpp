// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/shaderdump_compute.h"

namespace zx { namespace hwrender {

std::string ComputeShaderDumpName(const char *name, bool pixel)
{
	std::string out;
	for (const char *p = name != nullptr ? name : ""; *p != '\0'; ++p)
	{
		const char c = *p;
		const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
		if (keep) out += c;
		else if (!out.empty() && out.back() != '_') out += '_';
	}
	while (!out.empty() && out.back() == '_') out.pop_back();
	if (out.empty()) out = "shader";
	return out + (pixel ? ".frag" : ".vert");
}

}} // namespace zx::hwrender
