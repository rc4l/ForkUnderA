// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Which rows of the Customize Controls menu a typed search keeps, so a mod's long key list can be narrowed by name.
#ifndef ZX_CONTROLFILTER_COMPUTE_H
#define ZX_CONTROLFILTER_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// [Doruk-Aksoy] A blank line, a section title, or anything else (a key binding, a submenu) the search can match.
enum class ControlRowKind
{
	Spacer,
	Heading,
	Item,
};

// [Doruk-Aksoy] One row as the search sees it: its label and, for a binding, the command it binds.
struct ControlRow
{
	ControlRowKind kind;
	std::string label;
	std::string command;

	ControlRow() : kind(ControlRowKind::Item) {}
	ControlRow(ControlRowKind k, const std::string &l, const std::string &c = std::string()) : kind(k), label(l), command(c) {}
};

// [Doruk-Aksoy] Case-insensitive: `*` is any run and `?` any one character, anchored at both ends; a pattern with neither is a plain substring search.
bool ComputeControlPatternMatch(const std::string &pattern, const std::string &text);

// [Doruk-Aksoy] Indices of the rows to show: matching items under their heading, a whole section when its heading matches, and a blank line between sections.
std::vector<int> ComputeControlFilter(const std::vector<ControlRow> &rows, const std::string &pattern);

} // namespace zx

#endif // ZX_CONTROLFILTER_COMPUTE_H
