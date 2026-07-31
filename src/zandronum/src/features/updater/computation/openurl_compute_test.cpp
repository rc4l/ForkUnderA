// [rc4l] Tests for the open-URL safety allowlist. Every line/branch (the coverage gate enforces 100%
// on *_compute.cpp). This is a security boundary: it must ACCEPT ordinary http/https links and REFUSE
// every other scheme plus any control/space/high byte that could smuggle a shell command, a log-
// injection newline, or a homoglyph host. See features/updater/computation/openurl_compute.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/updater/computation/openurl_compute.h"

#include <gtest/gtest.h>
#include <string>

using zx::IsOpenableURL;
using zx::kMaxOpenableUrlLen;

// ---- accepted -------------------------------------------------------------

TEST(IsOpenableURL, AcceptsOrdinaryHttpAndHttps)
{
	EXPECT_TRUE(IsOpenableURL("https://github.com/rc4l/ZandroX/releases/latest"));
	EXPECT_TRUE(IsOpenableURL("http://example.com"));
	EXPECT_TRUE(IsOpenableURL("https://a")); // minimal: scheme + one host char
	EXPECT_TRUE(IsOpenableURL("https://host/path?q=1&r=2#frag"));
}

TEST(IsOpenableURL, SchemeIsCaseInsensitive)
{
	EXPECT_TRUE(IsOpenableURL("HTTPS://github.com/x"));
	EXPECT_TRUE(IsOpenableURL("HtTp://Example.Com/Path"));
}

// ---- rejected: null / empty / length -------------------------------------

TEST(IsOpenableURL, RejectsNullAndEmpty)
{
	EXPECT_FALSE(IsOpenableURL(nullptr));
	EXPECT_FALSE(IsOpenableURL(""));
}

TEST(IsOpenableURL, RejectsSchemeWithNoHost)
{
	EXPECT_FALSE(IsOpenableURL("http://"));
	EXPECT_FALSE(IsOpenableURL("https://"));
}

TEST(IsOpenableURL, EnforcesLengthCap)
{
	std::string ok = "https://x.com/";
	ok += std::string(kMaxOpenableUrlLen - static_cast<int>(ok.size()) - 1, 'a');
	EXPECT_EQ(static_cast<int>(ok.size()), kMaxOpenableUrlLen - 1);
	EXPECT_TRUE(IsOpenableURL(ok.c_str()));

	std::string tooLong = "https://x.com/" + std::string(kMaxOpenableUrlLen, 'a');
	EXPECT_FALSE(IsOpenableURL(tooLong.c_str()));
}

// ---- rejected: other schemes ---------------------------------------------

TEST(IsOpenableURL, RefusesDangerousSchemes)
{
	EXPECT_FALSE(IsOpenableURL("file:///etc/passwd"));
	EXPECT_FALSE(IsOpenableURL("javascript:alert(1)"));
	EXPECT_FALSE(IsOpenableURL("data:text/html,<script>1</script>"));
	EXPECT_FALSE(IsOpenableURL("mailto:a@b.com"));
	EXPECT_FALSE(IsOpenableURL("steam://run/379720"));
	EXPECT_FALSE(IsOpenableURL("ftp://host/f"));
	EXPECT_FALSE(IsOpenableURL("//host/path"));       // scheme-relative
	EXPECT_FALSE(IsOpenableURL("/local/path"));       // no scheme
	EXPECT_FALSE(IsOpenableURL("httpsx://host"));     // near-miss prefix
	EXPECT_FALSE(IsOpenableURL("http:/host"));        // missing a slash
	EXPECT_FALSE(IsOpenableURL("https:/"));           // proper prefix of the scheme, string ends early
	EXPECT_FALSE(IsOpenableURL("http"));              // bare word, no scheme
}

// ---- rejected: forbidden bytes -------------------------------------------

TEST(IsOpenableURL, RefusesControlSpaceAndHighBytes)
{
	EXPECT_FALSE(IsOpenableURL("https://host/a b"));       // raw space
	EXPECT_FALSE(IsOpenableURL("https://host/a\tb"));      // tab
	EXPECT_FALSE(IsOpenableURL("https://host/a\nb"));      // newline (log/command smuggling)
	EXPECT_FALSE(IsOpenableURL("https://host/a\rb"));      // CR
	EXPECT_FALSE(IsOpenableURL("https://host/\x7f"));      // DEL
	EXPECT_FALSE(IsOpenableURL("https://h\xc3\xa9st/"));   // non-ASCII (é) — homoglyph guard
	EXPECT_FALSE(IsOpenableURL("https://host/\x01"));      // low control char
}
