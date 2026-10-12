// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The player's visible set without GL: GL's BSP walk and clipper, marking what can be seen and building nothing.

#ifndef ZX_VISWALK_H
#define ZX_VISWALK_H

namespace zx { namespace visibility {

// [rc4l] Walk the BSP from the current view (viewx, viewy) and mark every subsector, sidedef and sector it reaches; `publish` hands the marks to the backend's walk bits.
void WalkView(bool publish = false);

// [rc4l] What the last walk reached; false before any walk this level.
bool SubsectorVisible(int subsector);
bool SideVisible(int side);
bool SectorVisible(int sector);
bool WalkedThisFrame();

// [rc4l] With fua_vis_check on, compare the last walk against the set GL's own walk marked this frame, counting what differs.
void CheckAgainstGL(const unsigned int *glBits, int words, int walkSlots, int mainSlot);

// [rc4l] Forget the level's walk, at level unload.
void ForgetWalk();

}} // namespace zx::visibility

#endif // ZX_VISWALK_H
