// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A music name as MAPINFO and TEAMINFO write one, with an optional ":order" track.

#ifndef ZX_MUSICSPEC_COMPUTE_H
#define ZX_MUSICSPEC_COMPUTE_H

#include <string>

namespace zx {

// [rc4l] A music name, its track and whether the text gave one after a colon.
struct MusicSpec
{
	std::string name;
	int order;
	bool hasOrder;
};

// [rc4l] Adapted from zandronum@59b494f63d585a9c5067c77ff228bb7fe2994cc0: "name:order" split as FMapInfoParser::ParseMusic does, so TEAMINFO's themes read the same way without borrowing a map parser.
MusicSpec ComputeMusicSpec(const char *text);

} // namespace zx

#endif
