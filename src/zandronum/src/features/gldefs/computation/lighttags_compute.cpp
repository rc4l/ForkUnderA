// [Doruk-Aksoy] The GLDEFS light tags and the blocks that take each.
#include "features/gldefs/computation/lighttags_compute.h"

#include <cstddef>
#include <cstdint>

namespace zx { namespace gldefs {

namespace {

struct TagName
{
	const char *word;
	LightTag tag;
};

const TagName kTagNames[] =
{
	{ "{", LightTag::OpenBrace },
	{ "}", LightTag::CloseBrace },
	{ "color", LightTag::Color },
	{ "size", LightTag::Size },
	{ "secondarySize", LightTag::SecondarySize },
	{ "offset", LightTag::Offset },
	{ "chance", LightTag::Chance },
	{ "interval", LightTag::Interval },
	{ "scale", LightTag::Scale },
	{ "frame", LightTag::Frame },
	{ "light", LightTag::Light },
	{ "subtractive", LightTag::Subtractive },
	{ "additive", LightTag::Additive },
	{ "halo", LightTag::Halo },
	{ "dontlightself", LightTag::DontLightSelf },
	{ "attenuate", LightTag::Attenuate },
	{ "dontlightactors", LightTag::DontLightActors },
	{ "spot", LightTag::Spot },
	{ "noshadowmap", LightTag::NoShadowmap },
	{ "dontlightothers", LightTag::DontLightOthers },
	{ "dontlightmap", LightTag::DontLightMap },
	{ "intensity", LightTag::Intensity },
};

char Lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

bool SameWord(const char *a, const char *b)
{
	for (; *a != '\0' && *b != '\0'; ++a, ++b)
	{
		if (Lower(*a) != Lower(*b)) return false;
	}
	return *a == *b;
}

constexpr uint32_t Bit(LightTag tag)
{
	return uint32_t(1) << unsigned(tag);
}

// [Doruk-Aksoy] What every light (not object or frame) takes, besides its own size, timing and blend tags.
constexpr uint32_t kEveryLight = Bit(LightTag::OpenBrace) | Bit(LightTag::CloseBrace) | Bit(LightTag::Color)
	| Bit(LightTag::Offset) | Bit(LightTag::Subtractive) | Bit(LightTag::Halo) | Bit(LightTag::DontLightSelf)
	| Bit(LightTag::Attenuate) | Bit(LightTag::DontLightActors) | Bit(LightTag::Spot) | Bit(LightTag::NoShadowmap)
	| Bit(LightTag::DontLightOthers) | Bit(LightTag::DontLightMap) | Bit(LightTag::Intensity);

constexpr uint32_t kBlockFrame = Bit(LightTag::OpenBrace) | Bit(LightTag::CloseBrace) | Bit(LightTag::Attenuate);

constexpr uint32_t kPulsing = kEveryLight | Bit(LightTag::Size) | Bit(LightTag::SecondarySize);

// [Doruk-Aksoy] Indexed by LightBlock, in its order.
constexpr uint32_t kAcceptedTags[] =
{
	kEveryLight | Bit(LightTag::Size) | Bit(LightTag::Additive),   // Point
	kPulsing | Bit(LightTag::Interval),                             // Pulse
	kPulsing | Bit(LightTag::Chance),                               // Flicker
	kPulsing | Bit(LightTag::Interval),                             // Flicker2
	kEveryLight | Bit(LightTag::Scale),                             // Sector
	kBlockFrame | Bit(LightTag::Frame),                             // Object
	kBlockFrame | Bit(LightTag::Light),                             // Frame
};
static_assert(sizeof(kAcceptedTags) / sizeof(kAcceptedTags[0]) == size_t(LightBlock::Frame) + 1, "one entry per LightBlock");

}

LightTag ComputeLightTag(const char *word)
{
	if (word == nullptr) return LightTag::Unknown;
	for (const TagName &name : kTagNames)
	{
		if (SameWord(word, name.word)) return name.tag;
	}
	return LightTag::Unknown;
}

bool ComputeLightTagAccepted(LightBlock block, LightTag tag)
{
	return tag != LightTag::Unknown && (kAcceptedTags[size_t(block)] & Bit(tag)) != 0;
}

} }
