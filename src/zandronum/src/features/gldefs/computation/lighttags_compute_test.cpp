// [Doruk-Aksoy] Tests for lighttags_compute: each GLDEFS light word names its tag, and each block takes exactly UZDoom's tags.
#include <gtest/gtest.h>

#include <initializer_list>

#include "features/gldefs/computation/lighttags_compute.h"

using namespace zx::gldefs;

namespace {

const LightTag kAllTags[] =
{
	LightTag::Unknown, LightTag::OpenBrace, LightTag::CloseBrace,
	LightTag::Color, LightTag::Size, LightTag::SecondarySize, LightTag::Offset, LightTag::Chance,
	LightTag::Interval, LightTag::Scale, LightTag::Frame, LightTag::Light,
	LightTag::Subtractive, LightTag::Additive, LightTag::Halo, LightTag::DontLightSelf, LightTag::Attenuate,
	LightTag::DontLightActors, LightTag::Spot, LightTag::NoShadowmap, LightTag::DontLightOthers,
	LightTag::DontLightMap, LightTag::Intensity,
};

// [Doruk-Aksoy] Every tag a light block of any kind takes, before its own size, timing and blend tags.
const std::initializer_list<LightTag> kEveryLight =
{
	LightTag::OpenBrace, LightTag::CloseBrace, LightTag::Color, LightTag::Offset, LightTag::Subtractive,
	LightTag::Halo, LightTag::DontLightSelf, LightTag::Attenuate, LightTag::DontLightActors, LightTag::Spot,
	LightTag::NoShadowmap, LightTag::DontLightOthers, LightTag::DontLightMap, LightTag::Intensity,
};

bool In(LightTag tag, std::initializer_list<LightTag> a, std::initializer_list<LightTag> b = {})
{
	for (LightTag t : a) if (t == tag) return true;
	for (LightTag t : b) if (t == tag) return true;
	return false;
}

// [Doruk-Aksoy] The block takes the tags in a and b and no other.
void ExpectTakesExactly(LightBlock block, std::initializer_list<LightTag> a, std::initializer_list<LightTag> b = {})
{
	for (LightTag tag : kAllTags)
	{
		EXPECT_EQ(ComputeLightTagAccepted(block, tag), In(tag, a, b)) << "block " << int(block) << " tag " << int(tag);
	}
}

}

// [Doruk-Aksoy] Each GLDEFS word names its tag.
TEST(ComputeLightTag, EveryWordNamesItsTag)
{
	EXPECT_EQ(ComputeLightTag("{"), LightTag::OpenBrace);
	EXPECT_EQ(ComputeLightTag("}"), LightTag::CloseBrace);
	EXPECT_EQ(ComputeLightTag("color"), LightTag::Color);
	EXPECT_EQ(ComputeLightTag("size"), LightTag::Size);
	EXPECT_EQ(ComputeLightTag("secondarySize"), LightTag::SecondarySize);
	EXPECT_EQ(ComputeLightTag("offset"), LightTag::Offset);
	EXPECT_EQ(ComputeLightTag("chance"), LightTag::Chance);
	EXPECT_EQ(ComputeLightTag("interval"), LightTag::Interval);
	EXPECT_EQ(ComputeLightTag("scale"), LightTag::Scale);
	EXPECT_EQ(ComputeLightTag("frame"), LightTag::Frame);
	EXPECT_EQ(ComputeLightTag("light"), LightTag::Light);
	EXPECT_EQ(ComputeLightTag("subtractive"), LightTag::Subtractive);
	EXPECT_EQ(ComputeLightTag("additive"), LightTag::Additive);
	EXPECT_EQ(ComputeLightTag("halo"), LightTag::Halo);
	EXPECT_EQ(ComputeLightTag("dontlightself"), LightTag::DontLightSelf);
	EXPECT_EQ(ComputeLightTag("attenuate"), LightTag::Attenuate);
	EXPECT_EQ(ComputeLightTag("dontlightactors"), LightTag::DontLightActors);
	EXPECT_EQ(ComputeLightTag("spot"), LightTag::Spot);
	EXPECT_EQ(ComputeLightTag("noshadowmap"), LightTag::NoShadowmap);
	EXPECT_EQ(ComputeLightTag("dontlightothers"), LightTag::DontLightOthers);
	EXPECT_EQ(ComputeLightTag("dontlightmap"), LightTag::DontLightMap);
	EXPECT_EQ(ComputeLightTag("intensity"), LightTag::Intensity);
}

// [Doruk-Aksoy] Case does not matter, as with FScanner::MatchString.
TEST(ComputeLightTag, IgnoresCase)
{
	EXPECT_EQ(ComputeLightTag("COLOR"), LightTag::Color);
	EXPECT_EQ(ComputeLightTag("SecondarySize"), LightTag::SecondarySize);
	EXPECT_EQ(ComputeLightTag("secondarysize"), LightTag::SecondarySize);
	EXPECT_EQ(ComputeLightTag("DontLightActors"), LightTag::DontLightActors);
}

// [Doruk-Aksoy] A word that is only a prefix of a tag, or runs past one, or is missing, is no tag.
TEST(ComputeLightTag, OtherWordsAreUnknown)
{
	EXPECT_EQ(ComputeLightTag(nullptr), LightTag::Unknown);
	EXPECT_EQ(ComputeLightTag(""), LightTag::Unknown);
	EXPECT_EQ(ComputeLightTag("colo"), LightTag::Unknown);
	EXPECT_EQ(ComputeLightTag("colors"), LightTag::Unknown);
	EXPECT_EQ(ComputeLightTag("shadowmap"), LightTag::Unknown);
	EXPECT_EQ(ComputeLightTag("Z"), LightTag::Unknown);
}

// [Doruk-Aksoy] A point light takes size and additive besides what every light takes.
TEST(ComputeLightTagAccepted, Point)
{
	ExpectTakesExactly(LightBlock::Point, kEveryLight, { LightTag::Size, LightTag::Additive });
}

// [Doruk-Aksoy] A pulse light takes both sizes and its interval, not additive.
TEST(ComputeLightTagAccepted, Pulse)
{
	ExpectTakesExactly(LightBlock::Pulse, kEveryLight, { LightTag::Size, LightTag::SecondarySize, LightTag::Interval });
}

// [Doruk-Aksoy] A flicker light takes both sizes and its chance.
TEST(ComputeLightTagAccepted, Flicker)
{
	ExpectTakesExactly(LightBlock::Flicker, kEveryLight, { LightTag::Size, LightTag::SecondarySize, LightTag::Chance });
}

// [Doruk-Aksoy] A random flicker light takes both sizes and its interval.
TEST(ComputeLightTagAccepted, Flicker2)
{
	ExpectTakesExactly(LightBlock::Flicker2, kEveryLight, { LightTag::Size, LightTag::SecondarySize, LightTag::Interval });
}

// [Doruk-Aksoy] A sector light takes a scale, no size.
TEST(ComputeLightTagAccepted, Sector)
{
	ExpectTakesExactly(LightBlock::Sector, kEveryLight, { LightTag::Scale });
}

// [Doruk-Aksoy] An object takes frames, a frame takes lights, and both still take attenuate as before.
TEST(ComputeLightTagAccepted, ObjectAndFrame)
{
	ExpectTakesExactly(LightBlock::Object, { LightTag::OpenBrace, LightTag::CloseBrace, LightTag::Attenuate, LightTag::Frame });
	ExpectTakesExactly(LightBlock::Frame, { LightTag::OpenBrace, LightTag::CloseBrace, LightTag::Attenuate, LightTag::Light });
}
