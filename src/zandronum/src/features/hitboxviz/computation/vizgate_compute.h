// [MGOOOOOO] Gating and driver-limit clamping for the debug hitbox overlay, pulled out pure so the
// two things most likely to be got wrong are pinned by tests rather than re-reasoned at each call
// site: what "cheats are enabled" actually has to mean here, and the fact that wide GL lines are
// not guaranteed to exist in a core profile.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#ifndef ZX_HITBOXVIZ_VIZGATE_COMPUTE_H
#define ZX_HITBOXVIZ_VIZGATE_COMPUTE_H

namespace zx { namespace hitboxviz {

// The overlay draws only when the user's toggle is on AND sv_cheats is actually true.
//
// It deliberately does NOT use CheckCheatmode(). That is the engine's usual cheat gate, but its
// contract is "are cheats permitted *here*", and in single-player it returns "permitted" whether or
// not sv_cheats is set -- which is why iddqd works offline. Routing this overlay through it made
// the boxes appear in single-player with cheats off. The requirement is the stricter one: no
// drawing unless cheats are enabled, in every game mode. Testing sv_cheats directly is also
// strictly stronger than CheckCheatmode -- that function can only refuse when sv_cheats is false --
// so nothing is lost by dropping it.
//
// Evaluating this every frame (rather than resetting the cvar from a callback, as Q-Zandronum's
// gl_show_hitbox does) means the user's menu preference survives joining a cheats-disabled server;
// the overlay simply stops drawing until cheats are enabled again.
//
// Caller beware: sv_cheats is CVAR_LATCH, so a mid-game `sv_cheats 1` does not take effect until
// the next map.
bool ShouldDraw(bool cvarEnabled, bool svCheats);

// Clamps a requested GL line width into the range the driver actually supports.
//
// glLineWidth() values above 1.0 are not required to work in an OpenGL core profile, so the
// requested width has to be clamped to GL_ALIASED_LINE_WIDTH_RANGE rather than passed through --
// an out-of-range width raises GL_INVALID_VALUE and the call is ignored, which would silently leave
// the overlay at whatever width was last set. A degenerate or inverted range is treated as "only
// glMin is available".
float ResolveLineWidth(float requested, float glMin, float glMax);

}} // namespace zx::hitboxviz

#endif // ZX_HITBOXVIZ_VIZGATE_COMPUTE_H
