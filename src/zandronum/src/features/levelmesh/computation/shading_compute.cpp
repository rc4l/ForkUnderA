// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/shading_compute.h"

#include <cmath>

namespace zx { namespace levelmesh {

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int ComputeLightLevel(int lightLevel, int relLight, bool weapon, const ShadingEnv &env)
{
	if (lightLevel == 0) return 0;
	int light = lightLevel;
	// [rc4l] xs_CRoundToInt rounds ties to even, as nearbyint does under the default rounding mode.
	if ((env.lightMode & 2) && lightLevel < 192 && !weapon)
		light = (int)std::nearbyint((double)(192.f - (192 - lightLevel) * 1.95f));
	if (light < env.ambient && env.lightMode != 8)
	{
		light = env.ambient;
		if (relLight < 0) relLight >>= 1;
	}
	return Clamp(light + relLight, 0, 255);
}

void ComputeLightColor(int light, unsigned color, int blendFactor, int lightMode, int &r, int &g, int &b)
{
	const int cr = (color >> 16) & 0xff, cg = (color >> 8) & 0xff, cb = color & 0xff;
	if (lightMode == 8) { r = cr; g = cg; b = cb; return; }
	if (blendFactor == 0)
	{
		r = cr * light / 255; g = cg * light / 255; b = cb * light / 255;
	}
	else
	{
		const int mix = light * (255 - blendFactor);
		r = (mix + cr * blendFactor) / 255; g = (mix + cg * blendFactor) / 255; b = (mix + cb * blendFactor) / 255;
	}
	r &= 0xff; g &= 0xff; b &= 0xff;
}

float ComputeDistFog(int distFog, bool softTable, int i)
{
	if (!softTable)
	{
		if (i < 164) return (float)((distFog >> 1) + distFog * (164 - i) / 164);
		if (i < 230) return (float)((distFog >> 1) - (distFog >> 1) * (i - 164) / (230 - 164));
		return 0.f;
	}
	if (i < 128) return 6.f + (distFog >> 1) + distFog * (128 - i) / 48;
	if (i < 216) return (216.f - i) / ((216.f - 128.f)) * distFog / 10;
	return 0.f;
}

float ComputeFogDensity(int lightLevel, unsigned fogColor, const ShadingEnv &env)
{
	if (env.lightMode & 4) return (float)(env.fogDensity ? env.fogDensity : 18);
	if ((fogColor & 0xffffff) == 0)
		return env.lightMode != 8 ? ComputeDistFog(env.distFog, env.lightMode != 0, Clamp(lightLevel, 0, 255)) : 0.f;
	if (env.outsideFogDensity != 0 && (env.outsideFogColor >> 24) != 0xff &&
	    (fogColor & 0xffffff) == (env.outsideFogColor & 0xffffff))
		return (float)env.outsideFogDensity;
	if (env.fogDensity != 0) return (float)env.fogDensity;
	if (lightLevel < 248) return (float)Clamp(255 - lightLevel, 30, 255);
	return 0.f;
}

Shading ComputeShading(const ShadingInput &in, const ShadingEnv &env)
{
	Shading s;
	int soft;
	if (env.fixedColormap)
	{
		s.r = s.g = s.b = 255 / 255.f;
		s.desaturation = 0;
		soft = 255;
	}
	else
	{
		int r, g, b;
		ComputeLightColor(ComputeLightLevel(in.lightLevel, in.relLight, false, env), in.lightColor, in.blendFactor,
			env.lightMode, r, g, b);
		s.r = r / 255.f; s.g = g / 255.f; s.b = b / 255.f;
		s.desaturation = in.desaturation;
		soft = Clamp(in.lightLevel + in.relLight, 0, 255);
	}
	s.softLight = env.lightMode == 8 ? soft / 255.f : -1.f;

	float density;
	unsigned color;
	if (env.fadeTable) { density = 70; color = 0x808080; }
	else if (!in.noFog && !env.fixedColormap)
	{
		density = ComputeFogDensity(in.lightLevel, in.fadeColor, env);
		color = in.fadeColor & 0xffffff;
	}
	else { density = 0; color = 0; }
	if (env.inSkybox) density += density / 2;

	// [rc4l] FRenderState::SetFog's scale, kept as it computes it so a disabled fog is the same -0 it stores.
	const float kScale = -1.442692f / 64000.f;
	if (density == 0 || env.fogMode == 0)
	{
		s.fogColor = 0;
		s.fogDensity = 0.f * kScale;
		s.fogMode = 0;
		return s;
	}
	if (in.additive) color = 0;
	s.fogColor = color;
	s.fogDensity = density * kScale;
	s.fogMode = color == 0 ? env.fogMode : -env.fogMode;
	if (env.lightMode == 8 && env.brightFog && color != 0) s.softLight = 255 / 255.f;
	return s;
}

bool ComputeSameLevelShading(const ShadingEnv &a, const ShadingEnv &b)
{
	return a.lightMode == b.lightMode && a.ambient == b.ambient && a.fogMode == b.fogMode && a.distFog == b.distFog &&
		a.fadeTable == b.fadeTable && a.brightFog == b.brightFog && a.fogDensity == b.fogDensity &&
		a.outsideFogDensity == b.outsideFogDensity && a.outsideFogColor == b.outsideFogColor;
}

}} // namespace zx::levelmesh
