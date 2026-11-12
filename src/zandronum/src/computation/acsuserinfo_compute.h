// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which userinfo a client sends, and when the server may kick for it.

#ifndef ZX_ACSUSERINFO_COMPUTE_H
#define ZX_ACSUSERINFO_COMPUTE_H

namespace zx {

// [rc4l] Adapted from zandronum@8cd5694e6ca6d454f5a179540a4fad6077f16a1c: never an unsynced cvar, and from a clientside script only a mod's; the server kicks for any other.
bool ComputeSendsUserinfoCvar(bool unsynced, bool modCvar, bool fromACS);

// [rc4l] Whether a userinfo command counts toward the flood kick: not while the full update is arriving, and never a script's changes.
bool ComputeUserinfoMayKick(bool fullUpdateIncomplete, bool fromACS);

} // namespace zx

#endif
