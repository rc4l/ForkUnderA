// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What the sky's three files share: the level's sky and the domes every sky draws with (dgsky.cpp), the skybox portal (dgskyportal.cpp), and other skies' pictures (dgskylooks.cpp).

#ifndef ZX_DGSKY_INTERNAL_H
#define ZX_DGSKY_INTERNAL_H

#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/hwrender/computation/skylook_compute.h"   // [rc4l] which sky a sector shows, and its dome's constants

namespace zx { namespace hwrender {

struct SkyDome;

// [rc4l] The dome pipeline, made once; false when it cannot be.
bool EnsureSkyPipeline();
// [rc4l] The sky a sector whose sky field is sectorSky shows this frame, as GLWall::SkyPlane reads the level, the transfer line and the scroll.
SkyLook LookOfSky(int sectorSky);
// [rc4l] The dome showing texture index texture, or NULL when it cannot be drawn.
SkyDome *DomeFor(int texture);
// [rc4l] A sky look as GLSkyPortal::DrawContents draws it: the main layer, a double sky's second over it, and the fade.
void DrawLook(Diligent::IDeviceContext *ctx, const SkyLook &look, const SkyDome *main, const SkyDome *second);

// [rc4l] Whether this frame's sky is the skybox portal's picture.
bool SkyPortalActive();
// [rc4l] Paints the portal's picture as the background; false when this frame's sky is not one.
bool BlitSkyPortal(Diligent::IDeviceContext *ctx);
// [rc4l] Under a plain sky dome, the sky surfaces in depth only before the world, as GL's sky walls reach to 32768 and hide whatever stands behind them.
void DrawSkyOccluders(Diligent::IDeviceContext *ctx);
bool DrawSkyPortalSurfaces(Diligent::IDeviceContext *ctx);
// [rc4l] The portal's pictures, pipelines and bindings, released with the sky's.
void ReleaseSkyPortal();

// [rc4l] A pass's sky surfaces that show another sky than the level's; false when none drew.
bool DrawSkyLookSurfaces(Diligent::IDeviceContext *ctx);
void ReleaseSkyLookPictures();

}} // namespace zx::hwrender

#endif // ZX_DGSKY_INTERNAL_H
