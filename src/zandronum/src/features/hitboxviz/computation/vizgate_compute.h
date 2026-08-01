// [MGOOOOOO] Gating and driver-limit clamping for the debug hitbox overlay, pulled out pure so the
// two things most likely to be got wrong are pinned by tests rather than re-reasoned at each call
// site: what "cheats are permitted" actually has to mean here, and the fact that wide GL lines are
// not guaranteed to exist in a core profile.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#ifndef ZX_HITBOXVIZ_VIZGATE_COMPUTE_H
#define ZX_HITBOXVIZ_VIZGATE_COMPUTE_H

namespace zx { namespace hitboxviz {

// The overlay draws when the user's toggle is on AND cheats are permitted here -- which means
// sv_cheats being true, OR the game being offline single-player, where sv_cheats is not the
// authority in the first place.
//
// `offlineGame` is "this machine is neither a client nor a server": nobody else is in the game and
// there is no server whose rules could be subverted, so the engine already permits cheating
// regardless of sv_cheats -- that is why iddqd works offline. Requiring sv_cheats here as well made
// the overlay the one debug view you could not use in the exact situation it is most useful: a
// local test map, where sv_cheats is latched and so needs a map change before it applies. Deliberate
// consequence: offline, the overlay obeys the toggle alone.
//
// The moment there IS someone to protect -- a client connected to a server, or the server itself --
// only sv_cheats decides, so cl_fua_hitbox_xray cannot become a wallhack anyone else's game has to
// live with. (Offline this is the same rule CheckCheatmode applies to iddqd, but it is spelled out
// from the two inputs rather than delegated: CheckCheatmode also refuses on DisableCheats skills,
// and a skill definition should not be able to switch off a debug renderer.)
//
// Evaluating this every frame (rather than resetting the cvar from a callback, as Q-Zandronum's
// gl_show_hitbox does) means the user's menu preference survives joining a cheats-disabled server;
// the overlay simply stops drawing until cheats are enabled again.
//
// Caller beware: sv_cheats is CVAR_LATCH, so on a server or as a client a mid-game `sv_cheats 1`
// does not take effect until the next map. Offline that no longer matters, since offlineGame alone
// is enough.
bool ShouldDraw(bool cvarEnabled, bool svCheats, bool offlineGame);

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
