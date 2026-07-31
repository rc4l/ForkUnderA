// [rc4l] Tests for the release download-URL builder. Every line/branch (the coverage gate enforces
// 100% on *_compute.cpp). Pins the exact asset naming from release.yml and the fallbacks.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/updater/computation/release_url_compute.h"

#include <gtest/gtest.h>
#include <cstring>

using zx::ComputeReleaseDownloadURL;
using zx::ComputeUpdateCheckResult;
using zx::ExtractVersionTag;
using zx::IsNewerVersion;
using zx::ParseLatestReleaseTag;
using zx::ReleasePlatform;
using zx::UpdateCheckStatus;

namespace {
const char *kRepo = "https://github.com/rc4l/ZandroX";
}

TEST(ReleaseDownloadURL, PerPlatformAssetNames)
{
	char buf[256];

	ASSERT_TRUE(ComputeReleaseDownloadURL(buf, sizeof buf, kRepo, "v0.1.19", ReleasePlatform::MacOS));
	EXPECT_STREQ(buf,
		"https://github.com/rc4l/ZandroX/releases/download/v0.1.19/ZandroX-v0.1.19-macos-arm64.zip");

	ASSERT_TRUE(ComputeReleaseDownloadURL(buf, sizeof buf, kRepo, "v0.1.19", ReleasePlatform::Windows));
	EXPECT_STREQ(buf,
		"https://github.com/rc4l/ZandroX/releases/download/v0.1.19/ZandroX-v0.1.19-windows-x64.zip");

	ASSERT_TRUE(ComputeReleaseDownloadURL(buf, sizeof buf, kRepo, "v0.1.19", ReleasePlatform::Linux));
	EXPECT_STREQ(buf,
		"https://github.com/rc4l/ZandroX/releases/download/v0.1.19/ZandroX-v0.1.19-linux-x86_64.tar.gz");
}

TEST(ReleaseDownloadURL, UnknownPlatformFallsBackToReleasePage)
{
	char buf[256];
	ASSERT_TRUE(ComputeReleaseDownloadURL(buf, sizeof buf, kRepo, "v0.1.19", ReleasePlatform::Unknown));
	EXPECT_STREQ(buf, "https://github.com/rc4l/ZandroX/releases/tag/v0.1.19");
}

TEST(ReleaseDownloadURL, RejectsBadInput)
{
	char buf[256];

	EXPECT_FALSE(ComputeReleaseDownloadURL(nullptr, sizeof buf, kRepo, "v1", ReleasePlatform::MacOS));
	EXPECT_FALSE(ComputeReleaseDownloadURL(buf, 0, kRepo, "v1", ReleasePlatform::MacOS));

	buf[0] = 'x';
	EXPECT_FALSE(ComputeReleaseDownloadURL(buf, sizeof buf, nullptr, "v1", ReleasePlatform::MacOS));
	EXPECT_STREQ(buf, ""); // cleared on bad input
	EXPECT_FALSE(ComputeReleaseDownloadURL(buf, sizeof buf, "", "v1", ReleasePlatform::MacOS));
	EXPECT_FALSE(ComputeReleaseDownloadURL(buf, sizeof buf, kRepo, nullptr, ReleasePlatform::MacOS));
	EXPECT_FALSE(ComputeReleaseDownloadURL(buf, sizeof buf, kRepo, "", ReleasePlatform::MacOS));
}

TEST(ReleaseDownloadURL, TruncationIsRefused)
{
	char small[20]; // far too short for any real URL
	EXPECT_FALSE(ComputeReleaseDownloadURL(small, sizeof small, kRepo, "v0.1.19", ReleasePlatform::MacOS));
	EXPECT_STREQ(small, ""); // cleared, not left half-written
}

TEST(ExtractVersionTag, StripsGitDescribeSuffix)
{
	char buf[32];
	ASSERT_TRUE(ExtractVersionTag("v0.1.18-37-g6744c8fe4", buf, sizeof buf));
	EXPECT_STREQ(buf, "v0.1.18");

	ASSERT_TRUE(ExtractVersionTag("v0.1.19", buf, sizeof buf)); // clean release, no suffix
	EXPECT_STREQ(buf, "v0.1.19");

	ASSERT_TRUE(ExtractVersionTag("v0.1.18+", buf, sizeof buf)); // dirty-tree marker
	EXPECT_STREQ(buf, "v0.1.18");
}

TEST(ExtractVersionTag, RejectsBadInput)
{
	char buf[32];
	EXPECT_FALSE(ExtractVersionTag(nullptr, buf, sizeof buf));
	EXPECT_FALSE(ExtractVersionTag("v1", nullptr, sizeof buf));
	EXPECT_FALSE(ExtractVersionTag("v1", buf, 0));

	buf[0] = 'x';
	EXPECT_FALSE(ExtractVersionTag("", buf, sizeof buf)); // empty describe
	EXPECT_STREQ(buf, "");

	// Leading delimiter -> zero-length tag -> false.
	EXPECT_FALSE(ExtractVersionTag("-37-gabc", buf, sizeof buf));
	EXPECT_STREQ(buf, "");
}

TEST(ExtractVersionTag, RefusesOverflow)
{
	char tiny[4]; // "v0.1.18" won't fit
	EXPECT_FALSE(ExtractVersionTag("v0.1.18-37-gabc", tiny, sizeof tiny));
	EXPECT_STREQ(tiny, ""); // cleared, not a truncated tag
}

TEST(IsNewerVersion, OrdersByMajorMinorPatch)
{
	EXPECT_TRUE(IsNewerVersion("v0.1.18", "v0.1.19"));  // patch bump
	EXPECT_TRUE(IsNewerVersion("v0.1.18", "v0.2.0"));   // minor bump
	EXPECT_TRUE(IsNewerVersion("v0.9.9", "v1.0.0"));    // major bump
	EXPECT_FALSE(IsNewerVersion("v0.1.19", "v0.1.18")); // older
	EXPECT_FALSE(IsNewerVersion("v0.1.19", "v0.1.19")); // equal is NOT newer
	EXPECT_FALSE(IsNewerVersion("v1.0.0", "v0.9.9"));   // older major
}

// ---- ParseLatestReleaseTag: robust to real + adversarial API bodies --------

TEST(ParseLatestReleaseTag, ExtractsTagFromRealisticJson)
{
	char buf[64];
	const char *json =
		"{\"url\":\"https://api.github.com/...\",\"tag_name\":\"v0.1.19\",\"name\":\"ZandroX v0.1.19\"}";
	ASSERT_TRUE(ParseLatestReleaseTag(json, buf, sizeof buf));
	EXPECT_STREQ(buf, "v0.1.19");
}

TEST(ParseLatestReleaseTag, ToleratesWhitespaceAndKeyOrder)
{
	char buf[64];
	EXPECT_TRUE(ParseLatestReleaseTag("{ \"tag_name\"  :  \"v1.2.3\" }", buf, sizeof buf));
	EXPECT_STREQ(buf, "v1.2.3");
	EXPECT_TRUE(ParseLatestReleaseTag("{\"tag_name\":\"v2.0.0\",\"draft\":false}", buf, sizeof buf));
	EXPECT_STREQ(buf, "v2.0.0");
	// key not first
	EXPECT_TRUE(ParseLatestReleaseTag("{\"id\":42,\n\"tag_name\":\"v9\"}", buf, sizeof buf));
	EXPECT_STREQ(buf, "v9");
}

TEST(ParseLatestReleaseTag, RejectsEmptyMissingAndNull)
{
	char buf[64];
	buf[0] = 'x';
	EXPECT_FALSE(ParseLatestReleaseTag("", buf, sizeof buf));            // empty body
	EXPECT_STREQ(buf, "");
	EXPECT_FALSE(ParseLatestReleaseTag("{\"name\":\"no tag here\"}", buf, sizeof buf)); // key absent
	EXPECT_FALSE(ParseLatestReleaseTag(nullptr, buf, sizeof buf));       // null body
	EXPECT_FALSE(ParseLatestReleaseTag("{\"tag_name\":\"\"}", buf, sizeof buf)); // empty value
	EXPECT_FALSE(ParseLatestReleaseTag(nullptr, nullptr, 0));            // bad buffer
	EXPECT_FALSE(ParseLatestReleaseTag("{\"tag_name\":\"v1\"}", buf, 0));
}

TEST(ParseLatestReleaseTag, RejectsTruncatedAndMalformed)
{
	char buf[64];
	// Response cut off mid-way (data arrived partial / stream closed early):
	EXPECT_FALSE(ParseLatestReleaseTag("{\"tag_name\":\"v0.1.1", buf, sizeof buf)); // unterminated value
	EXPECT_FALSE(ParseLatestReleaseTag("{\"tag_name\"", buf, sizeof buf));          // cut at the key
	EXPECT_FALSE(ParseLatestReleaseTag("{\"tag_name\":", buf, sizeof buf));         // cut after colon
	EXPECT_FALSE(ParseLatestReleaseTag("{\"tag_name\" \"v1\"}", buf, sizeof buf));  // missing colon
	EXPECT_FALSE(ParseLatestReleaseTag("{\"tag_name\":null}", buf, sizeof buf));    // value not a string
}

TEST(ParseLatestReleaseTag, RefusesOversizedValue)
{
	char small[8]; // "v0.1.19" (7) fits; a longer tag must be refused, not truncated
	EXPECT_TRUE(ParseLatestReleaseTag("{\"tag_name\":\"v0.1.1\"}", small, sizeof small));
	EXPECT_STREQ(small, "v0.1.1");
	EXPECT_FALSE(ParseLatestReleaseTag("{\"tag_name\":\"v0.1.100-rc1\"}", small, sizeof small));
	EXPECT_STREQ(small, "");
}

// ---- ComputeUpdateCheckResult: one place that folds fetch + parse + compare -

TEST(UpdateCheckResult, NoNetworkWhenFetchFails)
{
	// Covers timeout / no-network / DNS / HTTP error: fetchOk == false, whatever the body.
	auto r = ComputeUpdateCheckResult(false, "", "v0.1.18-37-gabc");
	EXPECT_EQ(r.status, UpdateCheckStatus::NoNetwork);
	auto r2 = ComputeUpdateCheckResult(false, "{\"tag_name\":\"v9.9.9\"}", "v0.1.18");
	EXPECT_EQ(r2.status, UpdateCheckStatus::NoNetwork); // a body we never trust because the GET failed
}

TEST(UpdateCheckResult, MalformedBodyNeverFalsePositives)
{
	for (const char *body : { "", "not json", "{\"tag_name\":\"v0.1.1", "{\"other\":1}" })
	{
		auto r = ComputeUpdateCheckResult(true, body, "v0.1.18");
		EXPECT_EQ(r.status, UpdateCheckStatus::Malformed) << "body: " << body;
		EXPECT_STREQ(r.tag, "");
	}
}

TEST(UpdateCheckResult, UpToDateForEqualOrOlder)
{
	auto eq = ComputeUpdateCheckResult(true, "{\"tag_name\":\"v0.1.18\"}", "v0.1.18-37-gabc");
	EXPECT_EQ(eq.status, UpdateCheckStatus::UpToDate); // latest == running build
	auto older = ComputeUpdateCheckResult(true, "{\"tag_name\":\"v0.1.17\"}", "v0.1.18");
	EXPECT_EQ(older.status, UpdateCheckStatus::UpToDate); // latest older than running build
}

TEST(UpdateCheckResult, UpdateAvailableForNewerReleasesTheTag)
{
	auto r = ComputeUpdateCheckResult(true, "{\"tag_name\":\"v0.2.0\"}", "v0.1.18-37-gabc");
	EXPECT_EQ(r.status, UpdateCheckStatus::UpdateAvailable);
	EXPECT_STREQ(r.tag, "v0.2.0");
	// Unknown/garbage current build version -> any real release counts as newer.
	auto r2 = ComputeUpdateCheckResult(true, "{\"tag_name\":\"v0.0.1\"}", "garbage");
	EXPECT_EQ(r2.status, UpdateCheckStatus::UpdateAvailable);
	EXPECT_STREQ(r2.tag, "v0.0.1");
	// Empty/unparseable current describe (ExtractVersionTag fails) -> treated as 0.0.0, still newer.
	auto r3 = ComputeUpdateCheckResult(true, "{\"tag_name\":\"v0.0.1\"}", "");
	EXPECT_EQ(r3.status, UpdateCheckStatus::UpdateAvailable);
	EXPECT_STREQ(r3.tag, "v0.0.1");
}

TEST(IsNewerVersion, ToleratesMissingPartsAndPrefix)
{
	EXPECT_TRUE(IsNewerVersion("v0.1", "v0.1.1"));      // current missing patch (=0) < .1
	EXPECT_FALSE(IsNewerVersion("v0.1", "v0.1.0"));     // .1.0 == .1
	EXPECT_TRUE(IsNewerVersion("0.1.18", "0.1.19"));    // no 'v' prefix
	EXPECT_TRUE(IsNewerVersion("V0.1.18", "v0.1.19"));  // mixed-case prefix
	EXPECT_TRUE(IsNewerVersion(nullptr, "v0.0.1"));     // null current -> 0.0.0
	EXPECT_FALSE(IsNewerVersion("v0.0.1", nullptr));    // null candidate -> 0.0.0
	EXPECT_FALSE(IsNewerVersion(nullptr, nullptr));     // both 0.0.0 -> equal
	EXPECT_FALSE(IsNewerVersion("v0.1.18", "garbage")); // non-numeric candidate -> 0.0.0
	EXPECT_TRUE(IsNewerVersion("garbage", "v0.0.1"));   // non-numeric current -> 0.0.0
}
