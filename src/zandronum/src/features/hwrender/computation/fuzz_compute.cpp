// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/fuzz_compute.h"

#include <math.h>

namespace zx { namespace hwrender {

namespace {

// [rc4l] GLSL's mod, which takes the floor rather than truncating.
float Mod(float x, float y) { return x - y * floorf(x / y); }

// [rc4l] The shaders' coordinate snapped down to 1/128, as float(int(x * 128.0)) / 128.0 truncates.
float Snap(float x) { return (float)(int)(x * 128.f) / 128.f; }

// [rc4l] The flicker every kind shares: two ratios of the pattern's coordinates, timed and folded into 0..0.5.
float Flicker(float texX, float texY, float timer)
{
	const float vX = (texX / texY) * 21.f;
	const float vY = (texY / texX) * 13.f;
	return Mod(timer * 2.f + (vX + vY), 0.5f);
}

} // namespace

int ComputeFuzzKind(int fuzzType, bool fuzzShader)
{
	return (fuzzShader && fuzzType > kFuzzNone && fuzzType < kFuzzKinds) ? fuzzType : kFuzzNone;
}

void ComputeFuzzSplat(float u, float v, float timer, float &su, float &sv)
{
	const float pi = 3.14159265358979323846f;
	su = u + Mod(sinf(pi * 2.f * (v + timer * 2.f)), 0.1f) * 0.1f;
	sv = v + Mod(cosf(pi * 2.f * (u + timer * 2.f)), 0.1f) * 0.1f;
}

void ComputeFuzzTexel(int kind, float u, float v, float timer, float rgba[4])
{
	float test = 1.f;
	switch (kind)
	{
	case kFuzzStandard: case kFuzzSmooth:
	{
		const float tu = kind == kFuzzStandard ? Snap(u) : u, tv = kind == kFuzzStandard ? Snap(v) : v;
		test = Flicker(tu / 3.f + 0.66f, 0.34f - tv / 3.f, timer);
		break;
	}
	case kFuzzSwirly: case kFuzzTranslucent: case kFuzzJagged:
		test = Flicker(sinf(u * 100.f + timer * 5.f), cosf(u * 100.f + timer * 5.f), timer);
		break;
	case kFuzzNoise: case kFuzzSmoothNoise:
	{
		const float tu = kind == kFuzzNoise ? Snap(u) : u, tv = kind == kFuzzNoise ? Snap(v) : v;
		test = Flicker(sinf(Mod(tu * 100.f + timer * 5.f, 3.489f)) + tu / 4.f, cosf(Mod(tv * 100.f + timer * 5.f, 3.489f)) + tv / 4.f, timer);
		break;
	}
	default:
		return;
	}
	rgba[3] *= test;
	if (kind != kFuzzTranslucent && kind != kFuzzJagged) rgba[0] = rgba[1] = rgba[2] = 0.f;
}

}} // namespace zx::hwrender
