// [rc4l] Pure blend-mode to backend-neutral factor/equation mapping plus 64-bit per-draw state-key packing for sorting and batching.
#ifndef ZX_HWRENDER_RENDERSTATE_COMPUTE_H
#define ZX_HWRENDER_RENDERSTATE_COMPUTE_H

#include <cstdint>

namespace hwrender
{

// [rc4l] High-level blend intents the draw layer asks for (mapped from the engine render style).
enum class BlendMode
{
	Opaque,
	Translucent,
	Additive,
	Modulate,
	Subtractive,
};

// [rc4l] Backend-neutral blend factors (backends map these to GL_*/VK_BLEND_FACTOR_*).
enum class BlendFactor
{
	Zero,
	One,
	SrcAlpha,
	InvSrcAlpha,
	DstColor,
};

// [rc4l] Backend-neutral blend equation.
enum class BlendEquation
{
	Add,
	ReverseSubtract,
};

// [rc4l] Resolved blend state for one draw.
struct BlendState
{
	bool enabled;
	BlendFactor src;
	BlendFactor dst;
	BlendEquation equation;
};

// [rc4l] Resolve a blend mode to concrete factors/equation. Unknown modes resolve to Opaque.
BlendState ComputeBlendState(BlendMode mode);

// [rc4l] Pack a draw's state into a 64-bit key so consecutive equal-state draws batch (see batch_compute) and draws can be state-sorted. Layout (high->low): shader:16 | texture:16 | blend:8 | reserved | depthTest:1 | cull:1.
uint64_t ComputeStateKey(uint16_t shader, uint16_t texture, BlendMode blend, bool depthTest, bool cull);

} // namespace hwrender

#endif // ZX_HWRENDER_RENDERSTATE_COMPUTE_H
