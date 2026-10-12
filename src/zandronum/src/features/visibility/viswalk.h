// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The player's visible set without GL: GL's BSP walk and clipper, marking what can be seen and building nothing.

#ifndef ZX_VISWALK_H
#define ZX_VISWALK_H

class AActor;
struct subsector_t;
struct sector_t;

namespace zx { namespace visibility {

// [rc4l] Walk the BSP from the current view as GL's walk does; `publish` hands what it reaches to the player's walk bits, `derive` makes the player's sky surfaces and collects the stacked views, skyboxes and mirrors it looks into, and `particles` takes each subsector reached with its sector as GL's walk sees it.
void WalkView(bool publish = false, bool derive = false, void (*particles)(subsector_t *sub, sector_t *sector) = 0);

// [rc4l] Walk each stacked view, skybox and line mirror the last derived WalkView looked into, from its own eye, as GL's portals walk them: their walk bits and sky surfaces, and each stacked view's and mirror's actors handed to `sprite` from inside that view.
void WalkPortalViews(void (*sprite)(AActor *thing));

// [rc4l] With fua_vis_check on, walk beside GL's own walk of the player's view and compare what the two reached and made; the stacked views, skyboxes and mirrors are compared once GL has walked them, by CheckPortalViews.
void CheckAgainstGL();
void CheckPortalViews();

// [rc4l] What every walk since the player's last derived WalkView reached (the player's view, a stacked view, a skybox, a mirror), each once: the sectors whose change re-bakes something reached (a subsector's own and render sector, a seg's two sectors, the control sectors of their 3D floors, a fill's model), and the sidedefs.
const int *SectorsWalkedThisFrame(int &count);
const int *SidesWalkedThisFrame(int &count);

// [rc4l] Forget the level's walk, at level unload.
void ForgetWalk();

}} // namespace zx::visibility

#endif // ZX_VISWALK_H
