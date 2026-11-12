// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/musicspec_compute.h"

#include <cstdlib>
#include <cstring>

namespace zx {

MusicSpec ComputeMusicSpec(const char *text)
{
	MusicSpec spec = { text != nullptr ? text : "", 0, false };
	const size_t colon = spec.name.find(':');
	if (colon != std::string::npos)
	{
		spec.order = atoi(spec.name.c_str() + colon + 1);
		spec.hasOrder = true;
		spec.name.erase(colon);
	}
	return spec;
}

} // namespace zx
