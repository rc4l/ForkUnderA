// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/visibility/skyfog.h"

namespace zx { namespace visibility {

// [rc4l] The sky's fade layer as the walk found it this frame, kept here since the walk derives it and the backend only draws it.
static float g_skyFog[4] = { 0.f, 0.f, 0.f, 0.f };

void SetSkyFog(int r, int g, int b, float a)
{
	g_skyFog[0] = r / 255.f; g_skyFog[1] = g / 255.f; g_skyFog[2] = b / 255.f; g_skyFog[3] = a;
}

void GetSkyFog(float &r, float &g, float &b, float &a)
{
	r = g_skyFog[0]; g = g_skyFog[1]; b = g_skyFog[2]; a = g_skyFog[3];
}

}} // namespace zx::visibility
