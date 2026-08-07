// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] What a scrolling viewport shows, and how far it moves to show something.
//
// A masked panel asks two questions of every row it draws, and they are NOT the same question. Is
// any of this row on screen -- if not, skip it entirely. And is ALL of this row on screen -- because
// that is what decides whether its lettering is drawn.
//
// The two-question split exists because clipping here is not uniform. Rectangles clip exactly: the
// panel gradient, the field backgrounds and the option cells all go through a clipped Dim and stop
// dead at the boundary. Text does not -- DrawText's clip tags do not survive the path the browser
// draws through, and a label that carries on past a background which has already stopped reads as a
// rendering fault rather than as something sliding under a mask. So a row on the edge shows its
// clipped background and no lettering, which is what the eye expects, and nothing depends on a clip
// the renderer will not honour.
//
// ScrollToReveal is the keyboard's half of it. Arrowing onto a row that is scrolled out of sight
// would move a focus the player cannot see, so the view follows the focus -- by the least it can, so
// that stepping down a form nudges rather than jumps.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_SCROLLVIEW_COMPUTE_H
#define ZX_SCROLLVIEW_COMPUTE_H

namespace zx
{

// Whether any part of the row falls inside the viewport, i.e. whether it is worth drawing at all.
// Half-open at both ends: a row that ends exactly on the top edge, or begins exactly on the bottom
// one, contributes no pixels and is not visible.
bool RowIntersectsView(int rowTop, int rowHeight, int viewTop, int viewBottom);

// Whether the WHOLE row falls inside the viewport, which is what gates its text. Closed at both
// ends, unlike the above: a row flush against an edge is entirely on screen.
bool RowFullyInView(int rowTop, int rowHeight, int viewTop, int viewBottom);

// A scroll offset brought back inside [0, maxScroll].
//
// Applied every frame rather than only when the scroll changes, because what it is measured against
// moves underneath it: a form one row shorter than it was leaves a scroll saved against the taller
// version pointing past the end, and the content sits above its own viewport with nothing to bring
// it back.
int ClampScroll(int scroll, int maxScroll);

// The scroll offset that brings a row into view, moving as little as possible and never past either
// end. `rowTop` is where the row sits NOW, with `scroll` already applied.
int ScrollToReveal(int scroll, int rowTop, int rowHeight, int viewTop, int viewBottom,
	int maxScroll);

} // namespace zx

#endif // ZX_SCROLLVIEW_COMPUTE_H
