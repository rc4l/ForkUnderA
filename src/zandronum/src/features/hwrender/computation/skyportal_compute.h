// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What the sky portal pass may draw: only what stands near its own camera, never the whole level.

#ifndef ZX_SKYPORTAL_COMPUTE_H
#define ZX_SKYPORTAL_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] Does a box in the map plane lie wholly outside the square `reach` either side of the camera?
bool ComputeBeyondSkyPortalReach(float minX, float maxX, float minY, float maxY,
                                 float camX, float camY, float reach);

// [rc4l] The eye a pass's constants carry (x, y, z in map units): the portal camera's in the sky portal pass, whose view the player's has replaced again by the time it draws, else the player's.
void ComputePassEye(bool skyPortalPass, const float *player, const float *portal, float *out);

}} // namespace zx::hwrender

#endif // ZX_SKYPORTAL_COMPUTE_H
