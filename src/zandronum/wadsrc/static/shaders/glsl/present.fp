// [rc4l] Shader gamma/brightness/contrast, applied when presenting the scene texture.
//
// This replaces the hardware gamma ramp (SDL_SetWindowGammaRamp / SetDeviceGammaRamp), which
// programs the DISPLAY's lookup table -- so it kept applying to the whole desktop after alt-tab
// and was only restored when the engine exited. Doing it here means it can only ever affect our
// own framebuffer.
//
// Adapted from uzdoom@81fd6c819fd5a6b71a946ba6e95cb67a76e4cac7 (shaders/pp/present.fp), taking
// only the gamma/contrast/brightness core -- upstream's current version also carries HDR, dither,
// saturation and white/black point, which depend on their postprocess uniform-block system.
//
// Operand order is upstream's correction from 72491049e0: brightness is folded in BEFORE the
// gamma pow, not after. Applying it after clipped negative brightness/contrast inappropriately.
// The max() guard is deliberately kept -- that same commit dropped it, but pow() of a negative
// base with a fractional exponent is undefined and upstream's present-day shader has it back.
// SPDX-License-Identifier: GPL-3.0-or-later

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D InputTexture;
uniform float InvGamma;
uniform float Contrast;
uniform float Brightness;

vec4 ApplyGamma(vec4 c)
{
	vec3 val = c.rgb * Contrast - (Contrast - 1.0) * 0.5;
	val += Brightness * 0.5;
	val = pow(max(val, vec3(0.0)), vec3(InvGamma));
	return vec4(val, c.a);
}

void main()
{
	FragColor = ApplyGamma(texture(InputTexture, TexCoord));
}
