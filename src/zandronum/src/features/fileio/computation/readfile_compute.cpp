// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/fileio/computation/readfile_compute.h"

namespace zx {

bool ComputeReadWhole(FILE *file, std::string &out, size_t maxBytes)
{
	out.clear();
	char chunk[4096];
	for (;;)
	{
		const size_t got = fread(chunk, 1, sizeof(chunk), file);
		if (got == 0) return ferror(file) == 0;
		if (got > maxBytes - out.size()) return false;
		out.append(chunk, got);
	}
}

bool ComputeReadWholeFile(const char *path, std::string &out, size_t maxBytes)
{
	out.clear();
	FILE *file = path != nullptr ? fopen(path, "rb") : nullptr;
	if (file == nullptr) return false;
	const bool whole = ComputeReadWhole(file, out, maxBytes);
	fclose(file);
	return whole;
}

std::string ComputeFileText(const char *path)
{
	std::string out;
	ComputeReadWholeFile(path, out);
	return out;
}

} // namespace zx
