// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Keeping a live-updating line from wobbling.
//
// The download band is sized from the text drawn on it, so anything that changes the text's WIDTH
// moves the panel. Padding the string to a fixed character count -- three columns for the
// percentage, the received figure padded to the width of the total -- got it to a fixed LENGTH, and
// it still moved, because a fixed number of characters is not a fixed number of pixels: SmallFont
// gives '1' a narrower advance than '0', so "11%" and "80%" are different widths.
//
// The fix is to measure a MASKED copy of the line and lay the band out from that. The mask is the
// same string every frame regardless of the numbers in it, so its width is a constant, and it is
// never narrower than the real line, so nothing overflows. The real text is what gets drawn; the mask
// only decides the box.
//
// SPACES ARE MASKED TOO, not just digits, and that is not an oversight. The padding that fixes the
// character count is made of spaces -- " 5%" against "99%" -- and a space is not the same width as a
// digit either, so masking only the digits leaves exactly the wobble this was meant to remove. A
// space can only ever be narrower than the widest digit, so substituting it keeps the mask an upper
// bound. The cost is a box a few pixels wider than it strictly needs to be, which is the right trade
// against one that moves.
//
// Which digit is widest is a question about the font, so the caller passes it in. That keeps this
// unit engine-free, and it is measured once rather than per frame.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_STABLELINE_COMPUTE_H
#define ZX_STABLELINE_COMPUTE_H

#include <string>

namespace zx
{

// Every ASCII digit and every space in `text` replaced by `widest`. Letters and punctuation are left
// alone: they do not change during a transfer, so they are already stable.
std::string MaskVarying( const std::string &text, char widest );

} // namespace zx

#endif // ZX_STABLELINE_COMPUTE_H
