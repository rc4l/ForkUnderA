// [rc4l] See sky_wire_compute.h.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "sky_wire_compute.h"

namespace zx
{

void CopySkyNameForWire(const char *src, char *out, size_t outSize)
{
	if (out == nullptr || outSize == 0)
		return;
	if (src == nullptr)
	{
		out[0] = '\0';
		return;
	}

	size_t i = 0;
	for (; i + 1 < outSize && src[i] != '\0'; ++i)
		out[i] = src[i];
	out[i] = '\0';
}

bool SkyNameFitsOnWire(const char *name)
{
	if (name == nullptr)
		return true;                       // empty is sent verbatim
	size_t n = 0;
	while (name[n] != '\0')
	{
		if (++n >= ZX_SKY_NAME_SIZE)       // would need a 9th character
			return false;
	}
	return true;
}

} // namespace zx
