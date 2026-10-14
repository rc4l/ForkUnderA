// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GL's fuzz shaders (wadsrc/static/shaders/glsl/fuzz_*.fp), which r_fuzztype 1-7 swaps in for a fuzzy sprite or weapon, as the backend's shaders run them.

#ifndef ZX_FUZZ_COMPUTE_H
#define ZX_FUZZ_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] r_fuzztype's shaders, in gl_shader.cpp's order: none, then standard, smooth, swirly, translucent, jagged, noise and smooth noise.
enum { kFuzzNone, kFuzzStandard, kFuzzSmooth, kFuzzSwirly, kFuzzTranslucent, kFuzzJagged, kFuzzNoise, kFuzzSmoothNoise, kFuzzKinds };

// [rc4l] The fuzz a sprite or weapon draws with: r_fuzztype's when its style resolved to a fuzz shader (GL's OverrideShader), else none.
int ComputeFuzzKind(int fuzzType, bool fuzzShader);

// [rc4l] Where the jagged kind samples its texture instead of at (u, v), `timer` seconds into the level.
void ComputeFuzzSplat(float u, float v, float timer, float &su, float &sv);

// [rc4l] The texel a fuzz kind leaves of `rgba` (sampled at the splat for the jagged kind) at (u, v): its alpha flickered by the pattern, its colour black except for the translucent and jagged kinds.
void ComputeFuzzTexel(int kind, float u, float v, float timer, float rgba[4]);

}} // namespace zx::hwrender

#endif // ZX_FUZZ_COMPUTE_H
