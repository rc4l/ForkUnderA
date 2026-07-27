// [rc4l] Covers every branch of the backend prerequisite check.
#include <gtest/gtest.h>

#include <string.h>

#include "backendprereq_compute.h"

namespace
{
// [rc4l] Everything present; individual tests knock one out.
BackendPrereqs AllPresent()
{
	BackendPrereqs p;
	p.frameBuffer = true;
	p.lightBuffer = true;
	p.boneBuffer = true;
	p.viewpointBuffer = true;
	p.vertexData = true;
	p.renderer = true;
	p.samplerManager = true;
	p.shaderManager = true;
	return p;
}
}

TEST(BackendPrereq, AllPresentReportsNothingMissing)
{
	EXPECT_EQ(nullptr, ComputeMissingPrereq(AllPresent()));
}

TEST(BackendPrereq, EachPrerequisiteIsNamedWhenAbsent)
{
	struct { bool BackendPrereqs::*field; const char *name; } cases[] =
	{
		{ &BackendPrereqs::frameBuffer,     "zx_screen" },
		{ &BackendPrereqs::lightBuffer,     "zx_screen->mLights" },
		{ &BackendPrereqs::boneBuffer,      "zx_screen->mBones" },
		{ &BackendPrereqs::viewpointBuffer, "zx_screen->mViewpoints" },
		{ &BackendPrereqs::vertexData,      "zx_screen->mVertexData" },
		{ &BackendPrereqs::renderer,        "GLRenderer" },
		{ &BackendPrereqs::samplerManager,  "GLRenderer->mSamplerManager" },
		{ &BackendPrereqs::shaderManager,   "GLRenderer->mShaderManager" },
	};

	for (const auto &c : cases)
	{
		BackendPrereqs p = AllPresent();
		p.*(c.field) = false;
		const char *missing = ComputeMissingPrereq(p);
		ASSERT_NE(nullptr, missing) << "expected " << c.name << " to be reported";
		EXPECT_STREQ(c.name, missing);
	}
}

TEST(BackendPrereq, ReportsTheFirstMissingInTouchOrder)
{
	// [rc4l] With several absent, the earliest one the backend touches must be named, so the message
	// points at the root cause rather than a downstream symptom.
	BackendPrereqs p = AllPresent();
	p.viewpointBuffer = false;
	p.samplerManager = false;
	EXPECT_STREQ("zx_screen->mViewpoints", ComputeMissingPrereq(p));

	p.frameBuffer = false;
	EXPECT_STREQ("zx_screen", ComputeMissingPrereq(p));
}

TEST(BackendPrereq, NothingPresentReportsTheFrameBuffer)
{
	BackendPrereqs p = {};
	EXPECT_STREQ("zx_screen", ComputeMissingPrereq(p));
}
