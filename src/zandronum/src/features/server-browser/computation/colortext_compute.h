// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Where a coloured string may be cut.
//
// Server names carry colour codes -- an operator writes "\cdColourful \chServer" and V_ColorizeString
// turns each "\c" into TEXTCOLOR_ESCAPE (0x1C), which the font renderer consumes along with the
// character after it. FFont::StringWidth already skips those, so measuring and drawing need no help.
//
// TRUNCATING does. Cutting a byte at a time to make a name fit will eventually land between the
// escape and the character it takes, leaving a dangling 0x1C that swallows the next glyph as a colour
// code instead of drawing it -- so the name loses a letter and possibly changes colour, from a
// truncation that was only supposed to shorten it. Worse, it only happens to names whose colour code
// falls near the cut, so it looks intermittent.
//
// This lists the byte offsets that are safe to cut at, and the drawer walks them from longest to
// shortest until the width fits.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_COLORTEXT_COMPUTE_H
#define ZX_COLORTEXT_COMPUTE_H

#include <cstddef>
#include <string>
#include <vector>

namespace zx
{

// The escape byte the font renderer treats as "a colour code follows". Matches TEXTCOLOR_ESCAPE in
// v_text.h; spelled out here so this unit stays engine-free.
const char kColorEscape = '\034';

// Byte offsets at which `text` may be cut without splitting a colour escape, ascending, always
// including 0 and text.size(). Handles both forms the renderer accepts: the escape plus one
// character, and the escape plus a bracketed name.
std::vector<size_t> ComputeColorSafeCutPoints(const std::string &text);

} // namespace zx

#endif // ZX_COLORTEXT_COMPUTE_H
