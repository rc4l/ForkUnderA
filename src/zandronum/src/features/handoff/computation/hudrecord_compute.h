// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] When the handoff records the level's HUD again: the game changes it only in a tic, and the screen's layout only when the screen, the view window or the HUD's own settings change.

#ifndef ZX_HUDRECORD_COMPUTE_H
#define ZX_HUDRECORD_COMPUTE_H

namespace zx { namespace handoff {

// [rc4l] What a HUD recording was made under.
struct HudKey
{
	int tic;
	int screenWidth, screenHeight;
	int viewHeight, screenBlocks;
	bool automap, viewActive, altHud, fullscreenHud;
};

// [rc4l] Whether a recording made under `made` (none when `valid` is false) is stale under `now`.
inline bool ComputeHUDRecordDue(bool valid, const HudKey &made, const HudKey &now)
{
	return !valid || made.tic != now.tic || made.screenWidth != now.screenWidth || made.screenHeight != now.screenHeight ||
		made.viewHeight != now.viewHeight || made.screenBlocks != now.screenBlocks || made.automap != now.automap ||
		made.viewActive != now.viewActive || made.altHud != now.altHud || made.fullscreenHud != now.fullscreenHud;
}

}} // namespace zx::handoff

#endif // ZX_HUDRECORD_COMPUTE_H
