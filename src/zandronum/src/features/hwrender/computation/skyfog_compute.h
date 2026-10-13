// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GLSkyPortal::DrawContents's fade layer: a sheet in the sky's sector fade colour over the dome, at the level's skyfog strength.

#ifndef ZX_SKYFOG_COMPUTE_H
#define ZX_SKYFOG_COMPUTE_H

namespace zx { namespace hwrender {

struct SkyFog { int r, g, b; float a; };

// [rc4l] The layer for a fade colour `fadeRGB` (0xRRGGBB); none without a skyfog, under a fixed colormap, or for a black fade.
SkyFog ComputeSkyFog(int skyfog, bool fixedColormap, unsigned int fadeRGB);

}} // namespace zx::hwrender

#endif // ZX_SKYFOG_COMPUTE_H
