// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The focus marker: a soft orb that sits beside whatever the keyboard is on.
//
// Shared, because there is now more than one place with a keyboard cursor of its own. The server
// browser had this orb and the global tab bar drew hairlines above and below the focused pill, so
// walking from a menu onto the bar changed what "you are here" looks like halfway through the
// gesture. One marker, drawn from one function, so it cannot drift apart again.
//
// Pairs with computation/glowtravel_compute, which owns the MOVEMENT between two positions. That
// unit is pure and this one only paints, so a caller can have the orb without the travel, or the
// travel without this, and neither knows about the other.

#ifndef ZX_FOCUSGLOW_H
#define ZX_FOCUSGLOW_H

namespace zx
{

// Paint the orb centred on a screen pixel.
//
// `scaleSpan` is how many REAL pixels the caller's 100 virtual units cover. Measured over a long
// span rather than one unit on purpose: the virtual-to-real mapping is fractional, so asking the
// width of a single virtual pixel rounds to 1 at some x and 2 at others, and the orb visibly changed
// size as it moved. A hundred units divides that rounding error by a hundred.
void DrawFocusGlow(int screenX, int screenY, int scaleSpan);

} // namespace zx

#endif // ZX_FOCUSGLOW_H
