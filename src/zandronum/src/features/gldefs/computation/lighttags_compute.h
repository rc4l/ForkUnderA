// [Doruk-Aksoy] Which word in a GLDEFS light block is which tag, and which blocks take it, so the five light blocks share one parser.
#ifndef ZX_LIGHTTAGS_COMPUTE_H
#define ZX_LIGHTTAGS_COMPUTE_H

namespace zx { namespace gldefs {

// [Doruk-Aksoy] A GLDEFS block that reads light tags.
enum class LightBlock
{
	Point, Pulse, Flicker, Flicker2, Sector, Object, Frame,
};

// [Doruk-Aksoy] A word inside a light block; Unknown is any word GLDEFS does not define.
enum class LightTag
{
	Unknown, OpenBrace, CloseBrace,
	Color, Size, SecondarySize, Offset, Chance, Interval, Scale, Frame, Light,
	Subtractive, Additive, Halo, DontLightSelf, Attenuate,
	DontLightActors, Spot, NoShadowmap, DontLightOthers, DontLightMap, Intensity,
};

// [Doruk-Aksoy] The tag a word names, matched without case as FScanner::MatchString does.
LightTag ComputeLightTag(const char *word);

// [Doruk-Aksoy] Whether a block takes a tag, the same set UZDoom's parser takes in each.
bool ComputeLightTagAccepted(LightBlock block, LightTag tag);

} }

#endif
