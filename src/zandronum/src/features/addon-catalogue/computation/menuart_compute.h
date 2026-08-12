// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Where an experience's picture lives, and where it goes when it is drawn.
//
// The catalogue can carry a small picture for each way of playing and each mix, pulled out of the
// packs themselves and shrunk to a few kilobytes. It stands in for the text header, so a player
// recognises a pack by the logo they already know rather than by reading its name.
//
// Two questions live here, and both are pure: what the file is CALLED, and where the picture goes
// inside the space it is given. Everything else -- reading it, turning it into a texture, drawing
// it -- needs engine types and belongs with the panel.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_MENUART_COMPUTE_H
#define ZX_MENUART_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// [rc4l] The picture for one way of playing, or for an entry that plays only one way.
//
// Named after the variant rather than numbered, so the file survives the list being reordered and
// so somebody looking at the folder can tell which is which. An empty id means the entry itself,
// which is the shape a pack with a single way of playing has.
//
// Beside the addon.json, like the cfgs, because it belongs to the same thing they do.
std::string MenuArtFileName(const std::string &variantId);

// Where one picture is drawn. Screen units, whatever the caller was working in.
struct ArtRect
{
	int x, y, w, h;

	ArtRect() : x(0), y(0), w(0), h(0) {}
};

// [rc4l] Fit some pictures into a slot, side by side, as one centred group.
//
// There are one or two: what you are playing, and what you are playing it with. Two is the reason
// this is not a one-liner -- they have to share the width, stay on their own aspect, and still read
// as a pair rather than as one picture and a gap.
//
// `sizes` are the pictures' own proportions, which is all that is wanted from them; the numbers
// need only be in the right ratio. A picture is scaled to the slot's HEIGHT and then held back if
// that would make it wider than its share, so a very wide logo loses height rather than crowding
// out whatever sits beside it.
//
// The group is centred in the slot, and each picture is centred vertically within it. An empty
// `sizes` gives an empty answer, which is the caller's cue to draw the text header instead.
std::vector<ArtRect> LayoutMenuArt(int slotX, int slotY, int slotW, int slotH, int gap,
                                   const std::vector<std::pair<int, int> > &sizes);

} // namespace zx

#endif // ZX_MENUART_COMPUTE_H
