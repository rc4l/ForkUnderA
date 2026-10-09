// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The sector light and fog as GLSL functions of their inputs, shared by the world shader and the horizon pass so they cannot drift.

#ifndef ZX_DGLIGHT_GLSL_H
#define ZX_DGLIGHT_GLSL_H

#define FUA_SECTOR_LIGHT_GLSL \
	"float R_DoomLightingEquation(float light, float dist) {\n" \
	"    float L = light * 63.0/31.0;\n" \
	"    float min_L = clamp(36.0/31.0 - L, 0.0, 1.0);\n" \
	"    if (dist < 0.0001) dist = 0.0001;\n" \
	"    float scale = 1.0 / dist;\n" \
	"    float index = (59.0/31.0 - L) - (scale * 232.0/31.0 - 232.0/31.0);\n" \
	"    return clamp(index, min_L, 1.0);\n" \
	"}\n" \
	/* [rc4l] exp2 of density times distance: view depth for the plain mode, radial for the other. */ \
	"float fuaFogFactor(float fogMode, float fogDensity, vec4 pixelPos, vec3 eye) {\n" \
	"    if (fogMode == 0.0) return 0.0;\n" \
	"    float fogdist = (abs(fogMode) < 1.5) ? pixelPos.w : max(16.0, distance(pixelPos.xyz, eye));\n" \
	"    return exp2(fogDensity * fogdist);\n" \
	"}\n" \
	/* [rc4l] The light colour before the texture: the Doom ramp, or plain fog's darkening, capped. */ \
	"vec3 fuaSectorLight(vec3 color, float softLight, float fogMode, float fogfactor, float fragZ) {\n" \
	"    if (softLight >= 0.0) color *= 1.0 - R_DoomLightingEquation(softLight, fragZ);\n" \
	"    else if (fogMode > 0.0) color = mix(vec3(0.0), color, fogfactor);\n" \
	"    return min(color, vec3(1.0));\n" \
	"}\n" \
	/* [rc4l] Coloured fog, laid over the finished fragment. */ \
	"vec3 fuaColouredFog(vec3 frag, vec3 fogColor, float fogMode, float fogfactor) {\n" \
	"    return (fogMode < 0.0) ? mix(fogColor, frag, fogfactor) : frag;\n" \
	"}\n"

#endif // ZX_DGLIGHT_GLSL_H
