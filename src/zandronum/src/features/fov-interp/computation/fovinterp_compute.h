// [rc4l] Pure FOV-tween arithmetic for the fov-interp feature.
//
// The engine moves a player's FOV toward its target one tic at a time (weapon zoom, the `fov`
// CCMD, A_SetFOV). That step is quantised to 35Hz, so at high refresh rates a zoom visibly
// staircases. Q-Zandronum's fix renders the *sub-tic* position of that same step; this unit is
// the arithmetic behind both halves, so the simulation step and the rendered position can never
// disagree — the render position is by construction a fraction of the very next sim step.
//
// Deliberately header-pure (no engine/GL/CVAR includes) so it stays replaceable: the renderer
// staircase replays `gl/scene/gl_scene.cpp` verbatim from upstream, and every line of tween logic
// living in that file would collide on each future flight. Here there is exactly one `[rc4l]`
// call site in gl_scene.cpp to re-apply instead of a 45-line hunk to hand-merge.
//
// Provenance: Q-Zandronum d2475b676 (2022-04-27, "Made FOV change interpolated in OpenGL
// renderer") and 390ea5ac2 (2023-04-16, "Stop FoV change interpolation when game is paused").
// The per-tic step itself is ZDoom's, unchanged since [RH]'s original in p_user.cpp; Q-Zandronum
// made its hardcoded 7.0 into the `cl_fovchangespeed` CVAR, which we follow.
// https://github.com/IgeNiaI/Q-Zandronum/commit/d2475b6760563f4be4b47c4eff0f82cee5a241c8
// https://github.com/IgeNiaI/Q-Zandronum/commit/390ea5ac290d5260415d458b8934518da1bd2289
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_FOVINTERP_COMPUTE_H
#define ZX_FOVINTERP_COMPUTE_H

namespace zx
{

// The FOV the player is actually heading toward: their desired FOV, scaled by the ready weapon's
// FOVScale when one applies. A negative scale is used upstream to stop G_AddViewAngle/Pitch
// scaling with it, so the magnitude is what matters here.
//   alive          - playerstate != PST_DEAD (no adjustment while dead)
//   hasReadyWeapon - ReadyWeapon != NULL     (no adjustment without a weapon)
//   weaponFovScale - ReadyWeapon->FOVScale   (0 means "no adjustment", not "collapse to zero")
float FovTargetForWeapon(float desiredFov, bool alive, bool hasReadyWeapon, float weaponFovScale);

// One tic of movement from currentFov toward targetFov. Snaps when the remaining distance is
// under the step, otherwise moves by max(changeSpeed, distance * 0.025) — the proportional term
// is what keeps a 90->10 zoom from crawling. Returns the new FOV.
float FovStepTic(float currentFov, float targetFov, float changeSpeed);

// How far to shift the rendered FOV this frame: the fraction `ticFrac` of the step the sim is
// about to take. Zero when interpolation is off (paused, in a menu, demo paused), which freezes
// the view exactly where the sim left it rather than drifting. ticFrac is clamped to [0,1].
float FovRenderDelta(float currentFov, float targetFov, float changeSpeed, float ticFrac,
                     bool interpolate);

// Clamp to the renderer's accepted range, matching R_SetFOV's 5..170 degrees. The interpolated
// value bypasses R_SetFOV (which quantises to fineangles and forces a resize), so the clamp has
// to be applied here instead.
float FovClamp(float fov);

// A change speed below 1 degree/tic would take minutes to converge and reads as a stuck view;
// the CVAR clamps on write, and this clamps again for callers that pass a raw value.
float FovChangeSpeedClamp(float changeSpeed);

} // namespace zx

#endif // ZX_FOVINTERP_COMPUTE_H
