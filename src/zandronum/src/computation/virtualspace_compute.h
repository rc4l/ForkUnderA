// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The mapping between a menu's layout space and the screen, and back again.
//
// Three menus were each carrying their own copy: the tab bar, the server browser and the Continue
// picker. Each copy's own comment says the inverse is "DERIVED FROM THE FORWARD MAPPING rather than
// reimplemented" -- which was true within a file and false across three of them.
//
// THE VIRTUAL SPACE TAKES THE SCREEN'S ASPECT rather than a fixed one, so the forward map is a plain
// scale and two rectangles sharing an edge cannot land a pixel apart. A layout is written against a
// nominal size and the space grows in whichever direction has room.
//
// THE INVERSE IS EVALUATED, NOT ALGEBRA. The forward map is affine, so sampling it at two points
// recovers the scale exactly and the round trip is correct by construction. Inverting the formula by
// hand gives a second rule that agrees with the first only until one of them is touched -- and the
// pointer lands where the second one says, while the drawing is where the first one put it.
//
// Header-pure by the features/ rules.

#ifndef ZX_VIRTUALSPACE_COMPUTE_H
#define ZX_VIRTUALSPACE_COMPUTE_H

namespace zx
{

struct VirtualSpace
{
	int width;
	int height;

	VirtualSpace() : width(640), height(400) {}
};

// The layout space for a screen of this size, given the size the layout was designed against.
// A zero or negative screen answers the nominal size rather than dividing by it.
VirtualSpace ComputeVirtualSpace(int screenW, int screenH, int layoutW, int layoutH);

// Layout units to screen pixels. Truncating, and taken as the difference of two mapped edges by the
// caller when it wants a width, the way the engine does it.
int VirtualToScreen(int v, int screenSize, int virtualSize);

// ...and back. `atZero` and `atSpan` are the forward map sampled at 0 and at `span`, so this cannot
// disagree with it. A span of zero pixels answers zero rather than dividing by it.
int ScreenToVirtual(int pixel, int atZero, int atSpan, int span);

} // namespace zx

#endif // ZX_VIRTUALSPACE_COMPUTE_H
