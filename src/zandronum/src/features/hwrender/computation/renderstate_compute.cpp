// [rc4l] Implementation of the blend-mode tables and state-key packing. No engine deps.
#include "features/hwrender/computation/renderstate_compute.h"

namespace hwrender
{

BlendState ComputeBlendState(BlendMode mode)
{
	switch (mode)
	{
	case BlendMode::Translucent:
		return BlendState{true, BlendFactor::SrcAlpha, BlendFactor::InvSrcAlpha, BlendEquation::Add};
	case BlendMode::Additive:
		return BlendState{true, BlendFactor::SrcAlpha, BlendFactor::One, BlendEquation::Add};
	case BlendMode::Modulate:
		return BlendState{true, BlendFactor::DstColor, BlendFactor::Zero, BlendEquation::Add};
	case BlendMode::Subtractive:
		return BlendState{true, BlendFactor::SrcAlpha, BlendFactor::One, BlendEquation::ReverseSubtract};
	case BlendMode::Opaque:
	default:
		return BlendState{false, BlendFactor::One, BlendFactor::Zero, BlendEquation::Add};
	}
}

uint64_t ComputeStateKey(uint16_t shader, uint16_t texture, BlendMode blend, bool depthTest, bool cull)
{
	uint64_t key = 0;
	key |= (uint64_t)shader << 48;
	key |= (uint64_t)texture << 32;
	key |= (uint64_t)((uint8_t)blend) << 24;
	key |= (uint64_t)(depthTest ? 1 : 0) << 1;
	key |= (uint64_t)(cull ? 1 : 0) << 0;
	return key;
}

} // namespace hwrender
