// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/models/computation/fuamodel_compute.h"

#include <cctype>

namespace zx { namespace models {

static bool SameWord(const std::string &a, const char *b)
{
	size_t i = 0;
	for (; i < a.size() && b[i]; i++)
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
	return i == a.size() && b[i] == 0;
}

FuaModelDefs ComputeParseFuaModel(const char *text, size_t length)
{
	FuaModelDefs out;
	size_t at = 0;
	int line = 0;
	while (at < length)
	{
		line++;
		size_t end = at;
		while (end < length && text[end] != '\n') end++;
		std::vector<std::string> words;
		for (size_t i = at; i < end;)
		{
			if (text[i] == '/' && i + 1 < end && text[i + 1] == '/') break;
			if (std::isspace((unsigned char)text[i])) { i++; continue; }
			size_t j = i;
			while (j < end && !std::isspace((unsigned char)text[j]) && !(text[j] == '/' && j + 1 < end && text[j + 1] == '/')) j++;
			words.push_back(std::string(text + i, j - i));
			i = j;
		}
		at = end + 1;
		if (words.empty()) continue;
		if (!SameWord(words[0], "FlatSurface"))
			out.errors.push_back("line " + std::to_string(line) + ": unknown keyword '" + words[0] + "'");
		else if (words.size() == 1)
			out.errors.push_back("line " + std::to_string(line) + ": FlatSurface names no actor");
		else
			out.flatSurfaces.insert(out.flatSurfaces.end(), words.begin() + 1, words.end());
	}
	return out;
}

}} // namespace zx::models
