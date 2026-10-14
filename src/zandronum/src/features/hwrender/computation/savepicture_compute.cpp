// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/savepicture_compute.h"

#include <cstring>

namespace zx {

static const unsigned char kSignature[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };

// [rc4l] The chunks M_CreatePNG and M_CreateDummyPNG write for the picture, before the save's own.
static bool IsPictureChunk(const unsigned char *type)
{
	static const char *const kinds[] = { "IHDR", "gAMA", "PLTE", "IDAT" };
	for (const char *kind : kinds)
		if (memcmp(type, kind, 4) == 0) return true;
	return false;
}

size_t ComputePngPictureLength(const unsigned char *bytes, size_t size)
{
	if (size < sizeof(kSignature) || memcmp(bytes, kSignature, sizeof(kSignature)) != 0) return 0;
	size_t at = sizeof(kSignature);
	while (size - at >= 8 && IsPictureChunk(bytes + at + 4))
	{
		const size_t len = (size_t)bytes[at] << 24 | (size_t)bytes[at + 1] << 16 | (size_t)bytes[at + 2] << 8 | bytes[at + 3];
		// [rc4l] Length, type and CRC around the data.
		if (size - at < 12 || len > size - at - 12) return 0;
		at += 12 + len;
	}
	return at == sizeof(kSignature) ? 0 : at;
}

bool ComputeSpliceSavePicture(const std::string &save, const std::string &picture, std::string &out)
{
	const size_t oldPicture = ComputePngPictureLength((const unsigned char *)save.data(), save.size());
	const size_t newPicture = ComputePngPictureLength((const unsigned char *)picture.data(), picture.size());
	if (oldPicture == 0 || newPicture == 0) return false;
	out = picture.substr(0, newPicture) + save.substr(oldPicture);
	return true;
}

} // namespace zx
