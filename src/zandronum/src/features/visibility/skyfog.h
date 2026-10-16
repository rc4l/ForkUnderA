// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The sky's fade layer: the translucent sheet in the sector's fade colour GL draws over the dome, set each frame the walk finds it and zeroed when it is not drawn.

#ifndef ZX_SKYFOG_H
#define ZX_SKYFOG_H

namespace zx { namespace visibility {

void SetSkyFog(int r, int g, int b, float a);
void GetSkyFog(float &r, float &g, float &b, float &a);

}} // namespace zx::visibility

#endif // ZX_SKYFOG_H
