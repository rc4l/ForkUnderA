// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Scrollbar geometry: where the thumb goes, how big it is, and what a click on the track means.
//
// Three numbers that have to agree, and did not. The drawing worked out a thumb height and the mouse
// code did not -- it mapped the pointer straight down the whole track -- so clicking anywhere but the
// very top jumped somewhere else entirely, and the error grew with the thumb. With half the list
// visible the thumb is half the track and the click lands about half a page out, which reads as the
// bar simply not working.
//
// The rule the mapping has to respect: the thumb TRAVELS over (track - thumb), not over the track.
// Its top runs from 0 to (track - thumb), because the rest of the track is the thumb's own body. Any
// mapping that divides by the track height instead is wrong by exactly one thumb, every time.
//
// And a click positions the thumb's CENTRE at the pointer, not its top -- grab the middle of a
// scrollbar anywhere and that is what happens. Mapping to the top makes every click feel like it
// undershoots by half a thumb, which is the other half of the same bug.
//
// Both draw and hit-test go through these, so they cannot disagree again.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_SCROLLBAR_COMPUTE_H
#define ZX_SCROLLBAR_COMPUTE_H

namespace zx
{

// Height of the thumb for a list showing `visible` of `total`, never below `minThumb` and never
// larger than the track. A proportional thumb on a list of hundreds rounds to nothing, and a bar
// whose thumb has vanished looks broken rather than full.
int ComputeThumbHeight(int trackHeight, int visible, int total, int minThumb);

// Offset of the thumb's top within the track, for a window starting at row `first`. `maxFirst` is
// the largest that can be, i.e. total - visible.
int ComputeThumbTop(int trackHeight, int thumbHeight, int first, int maxFirst);

// The row the window should start at when the pointer is `pointerOffset` down the track. The inverse
// of ComputeThumbTop, centred: the thumb's middle lands under the pointer. Clamped to [0, maxFirst],
// so dragging past either end of the track pins rather than running away.
int ComputeFirstFromPointer(int pointerOffset, int trackHeight, int thumbHeight, int maxFirst);

} // namespace zx

#endif // ZX_SCROLLBAR_COMPUTE_H
