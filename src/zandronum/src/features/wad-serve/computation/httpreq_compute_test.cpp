// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/wad-serve/computation/httpreq_compute.h"

using zx::HttpParse;
using zx::HttpRange;
using zx::HttpRequest;
using zx::ParseHttpRequest;
using zx::ParseRangeHeader;
using zx::PercentDecode;
using zx::ResolveRange;
using std::string;

namespace
{
const size_t kCap = 8192;

HttpParse Parse(const string &raw, HttpRequest &out) { return ParseHttpRequest(raw, kCap, out); }

HttpParse ParseOnly(const string &raw)
{
	HttpRequest ignored;
	return ParseHttpRequest(raw, kCap, ignored);
}

string Get(const string &target) { return "GET " + target + " HTTP/1.1\r\n\r\n"; }
} // namespace

// ---------------------------------------------------------------- percent decoding

TEST(PercentDecode, PassesPlainTextThrough)
{
	string out;
	ASSERT_TRUE(PercentDecode("dwango5.wad", out));
	EXPECT_EQ("dwango5.wad", out);
}

TEST(PercentDecode, DecodesEscapesInEitherCase)
{
	string out;
	ASSERT_TRUE(PercentDecode("a%20b%2Fc%2fd", out));
	EXPECT_EQ("a b/c/d", out);
}

TEST(PercentDecode, DecodesEveryHexDigitClass)
{
	string out;
	ASSERT_TRUE(PercentDecode("%41%61%39", out));		// upper, lower, digit
	EXPECT_EQ("Aa9", out);
}

TEST(PercentDecode, RejectsATruncatedEscape)
{
	// A lenient decoder here and a strict one elsewhere is how the same bytes come to mean two
	// different filenames.
	string out;
	EXPECT_FALSE(PercentDecode("abc%2", out));
	EXPECT_FALSE(PercentDecode("abc%", out));
}

TEST(PercentDecode, RejectsNonHexAfterThePercent)
{
	string out;
	EXPECT_FALSE(PercentDecode("a%zz", out));
	EXPECT_FALSE(PercentDecode("a%2z", out));
}

// ---------------------------------------------------------------- Range header

TEST(ParseRangeHeader, ReadsAClosedRange)
{
	HttpRange r;
	ASSERT_TRUE(ParseRangeHeader("bytes=100-199", r));
	EXPECT_TRUE(r.present);
	EXPECT_FALSE(r.suffix);
	EXPECT_EQ(100, r.first);
	EXPECT_EQ(199, r.last);
}

TEST(ParseRangeHeader, ReadsAnOpenEndedRange)
{
	HttpRange r;
	ASSERT_TRUE(ParseRangeHeader(" bytes=500- ", r));
	EXPECT_EQ(500, r.first);
	EXPECT_EQ(-1, r.last) << "-1 is how 'through the end' is spelled";
}

TEST(ParseRangeHeader, ReadsTheSuffixFormAsACount)
{
	// "bytes=-500" is the LAST 500 bytes, not everything from offset 500 -- nearly identical to
	// read, completely different to serve.
	HttpRange r;
	ASSERT_TRUE(ParseRangeHeader("bytes=-500", r));
	EXPECT_TRUE(r.suffix);
	EXPECT_EQ(500, r.last);
}

TEST(ParseRangeHeader, IsCaseInsensitiveOnTheUnit)
{
	HttpRange r;
	EXPECT_TRUE(ParseRangeHeader("BYTES=0-1", r));
}

TEST(ParseRangeHeader, ToleratesTabPadding)
{
	HttpRange r;
	ASSERT_TRUE(ParseRangeHeader("\tbytes=\t7-9\t", r));
	EXPECT_EQ(7, r.first);
	EXPECT_EQ(9, r.last);
}

TEST(ParseRangeHeader, RejectsAnythingItWillNotServe)
{
	HttpRange r;
	EXPECT_FALSE(ParseRangeHeader("", r));
	EXPECT_FALSE(ParseRangeHeader("bytes=", r)) << "no spec at all";
	EXPECT_FALSE(ParseRangeHeader("items=0-1", r)) << "wrong unit";
	EXPECT_FALSE(ParseRangeHeader("bytes=0-1,5-6", r)) << "multi-range needs multipart responses";
	EXPECT_FALSE(ParseRangeHeader("bytes=0100", r)) << "no dash";
	EXPECT_FALSE(ParseRangeHeader("bytes=-", r)) << "a dash and nothing either side";
	EXPECT_FALSE(ParseRangeHeader("bytes=-abc", r)) << "suffix count not a number";
	EXPECT_FALSE(ParseRangeHeader("bytes=abc-9", r)) << "start not a number";
	EXPECT_FALSE(ParseRangeHeader("bytes=1-abc", r)) << "end not a number";
	EXPECT_FALSE(ParseRangeHeader("bytes=1234567890123456789-", r)) << "implausibly long number";
}

// ---------------------------------------------------------------- resolving a range

TEST(ResolveRange, AbsentRangeIsTheWholeFile)
{
	HttpRange none;
	long long offset = -1, length = -1;
	ASSERT_TRUE(ResolveRange(none, 4096, offset, length));
	EXPECT_EQ(0, offset);
	EXPECT_EQ(4096, length);
}

TEST(ResolveRange, ClosedRangeIsInclusiveOfBothEnds)
{
	// The off-by-one that HTTP ranges exist to cause: 0-0 is one byte, not zero.
	HttpRange r;
	ASSERT_TRUE(ParseRangeHeader("bytes=0-0", r));

	long long offset = -1, length = -1;
	ASSERT_TRUE(ResolveRange(r, 100, offset, length));
	EXPECT_EQ(0, offset);
	EXPECT_EQ(1, length);
}

TEST(ResolveRange, OpenEndedRangeRunsToTheEnd)
{
	HttpRange r;
	ASSERT_TRUE(ParseRangeHeader("bytes=90-", r));

	long long offset = -1, length = -1;
	ASSERT_TRUE(ResolveRange(r, 100, offset, length));
	EXPECT_EQ(90, offset);
	EXPECT_EQ(10, length);
}

TEST(ResolveRange, ClampsAnEndPastTheFile)
{
	HttpRange r;
	ASSERT_TRUE(ParseRangeHeader("bytes=90-99999", r));

	long long offset = -1, length = -1;
	ASSERT_TRUE(ResolveRange(r, 100, offset, length));
	EXPECT_EQ(90, offset);
	EXPECT_EQ(10, length);
}

TEST(ResolveRange, SuffixTakesTheTailOfTheFile)
{
	HttpRange r;
	ASSERT_TRUE(ParseRangeHeader("bytes=-30", r));

	long long offset = -1, length = -1;
	ASSERT_TRUE(ResolveRange(r, 100, offset, length));
	EXPECT_EQ(70, offset);
	EXPECT_EQ(30, length);
}

TEST(ResolveRange, SuffixLongerThanTheFileIsTheWholeFile)
{
	HttpRange r;
	ASSERT_TRUE(ParseRangeHeader("bytes=-500", r));

	long long offset = -1, length = -1;
	ASSERT_TRUE(ResolveRange(r, 100, offset, length));
	EXPECT_EQ(0, offset);
	EXPECT_EQ(100, length);
}

TEST(ResolveRange, RefusesWhatItCannotSatisfy)
{
	long long offset = 0, length = 0;
	HttpRange r;

	ASSERT_TRUE(ParseRangeHeader("bytes=-0", r));
	EXPECT_FALSE(ResolveRange(r, 100, offset, length)) << "a zero-length suffix asks for nothing";

	ASSERT_TRUE(ParseRangeHeader("bytes=-5", r));
	EXPECT_FALSE(ResolveRange(r, 0, offset, length)) << "no bytes to take the tail of";

	// Starting past the end means the client's idea of the file is stale -- a 416, not a truncated
	// 200 it would treat as the real thing.
	ASSERT_TRUE(ParseRangeHeader("bytes=100-", r));
	EXPECT_FALSE(ResolveRange(r, 100, offset, length));

	ASSERT_TRUE(ParseRangeHeader("bytes=50-10", r));
	EXPECT_FALSE(ResolveRange(r, 100, offset, length)) << "end before start";

	HttpRange none;
	EXPECT_FALSE(ResolveRange(none, -1, offset, length)) << "no such thing as a negative file";
}

// ---------------------------------------------------------------- request parsing

TEST(ParseHttpRequest, ReadsAMinimalGet)
{
	HttpRequest req;
	ASSERT_EQ(HttpParse::Ok, Parse(Get("/dwango5.wad"), req));
	EXPECT_EQ("GET", req.method);
	EXPECT_EQ("dwango5.wad", req.filename);
	EXPECT_FALSE(req.headOnly);
	EXPECT_FALSE(req.range.present);
}

TEST(ParseHttpRequest, ReadsHeadAsHeadersOnly)
{
	HttpRequest req;
	ASSERT_EQ(HttpParse::Ok, Parse("HEAD /a.wad HTTP/1.1\r\n\r\n", req));
	EXPECT_TRUE(req.headOnly);
}

TEST(ParseHttpRequest, PicksUpARangeHeaderAmongOthers)
{
	HttpRequest req;
	const string raw =
		"GET /a.wad HTTP/1.1\r\n"
		"Host: example\r\n"
		"RANGE: bytes=10-19\r\n"
		"User-Agent: curl\r\n"
		"\r\n";
	ASSERT_EQ(HttpParse::Ok, Parse(raw, req));
	ASSERT_TRUE(req.range.present);
	EXPECT_EQ(10, req.range.first);
	EXPECT_EQ(19, req.range.last);
}

TEST(ParseHttpRequest, IgnoresARangeItCannotParse)
{
	// Sending the whole file is always a correct answer; a download that succeeds beats one that 400s.
	HttpRequest req;
	ASSERT_EQ(HttpParse::Ok, Parse("GET /a.wad HTTP/1.1\r\nRange: furlongs=1-2\r\n\r\n", req));
	EXPECT_FALSE(req.range.present);
}

TEST(ParseHttpRequest, IgnoresAHeaderLineWithNoColon)
{
	HttpRequest req;
	EXPECT_EQ(HttpParse::Ok, Parse("GET /a.wad HTTP/1.1\r\ngarbage\r\n\r\n", req));
}

TEST(ParseHttpRequest, StripsAQueryString)
{
	HttpRequest req;
	ASSERT_EQ(HttpParse::Ok, Parse(Get("/a.wad?v=2"), req));
	EXPECT_EQ("a.wad", req.filename);
}

TEST(ParseHttpRequest, DecodesTheFilename)
{
	HttpRequest req;
	ASSERT_EQ(HttpParse::Ok, Parse(Get("/my%20mod.wad"), req));
	EXPECT_EQ("my mod.wad", req.filename);
}

TEST(ParseHttpRequest, WantsMoreUntilTheHeaderBlockTerminates)
{
	EXPECT_EQ(HttpParse::NeedMore, ParseOnly("GET /a.wad HTTP/1.1\r\n"));
	EXPECT_EQ(HttpParse::NeedMore, ParseOnly(""));
}

TEST(ParseHttpRequest, RefusesAHeaderBlockOverTheCap)
{
	// The slowloris bound: a peer must not be able to dribble headers forever and pin a slot.
	HttpRequest req;
	const string unterminated = "GET /a.wad HTTP/1.1\r\nX: " + string(200, 'a');
	EXPECT_EQ(HttpParse::TooLarge, ParseHttpRequest(unterminated, 64, req));

	// And a complete-but-oversized block is refused just the same.
	EXPECT_EQ(HttpParse::TooLarge, ParseHttpRequest(unterminated + "\r\n\r\n", 64, req));
}

TEST(ParseHttpRequest, RefusesMethodsItDoesNotServe)
{
	EXPECT_EQ(HttpParse::Unsupported, ParseOnly("PUT /a.wad HTTP/1.1\r\n\r\n"));
	EXPECT_EQ(HttpParse::Unsupported, ParseOnly("get /a.wad HTTP/1.1\r\n\r\n"))
		<< "HTTP methods are case-sensitive";
}

TEST(ParseHttpRequest, RefusesAMalformedRequestLine)
{
	EXPECT_EQ(HttpParse::BadRequest, ParseOnly("GET\r\n\r\n")) << "no target, no version";
	EXPECT_EQ(HttpParse::BadRequest, ParseOnly("GET /a.wad\r\n\r\n")) << "no version";
	EXPECT_EQ(HttpParse::BadRequest, ParseOnly("GET /a.wad HTTP/2\r\n\r\n"));
	EXPECT_EQ(HttpParse::BadRequest, ParseOnly("GET  HTTP/1.1\r\n\r\n")) << "empty target";
	EXPECT_EQ(HttpParse::BadRequest, ParseOnly("GET a.wad HTTP/1.1\r\n\r\n")) << "target not rooted";
	EXPECT_EQ(HttpParse::BadRequest, ParseOnly(Get("/?v=2"))) << "nothing left after the query";
}

TEST(ParseHttpRequest, RefusesAnythingThatNamesAPathRatherThanAFile)
{
	// The structural defence: a request that cannot name a directory cannot name a parent one, so
	// traversal is not a check that has to be right -- it is a shape the parser will not produce.
	EXPECT_EQ(HttpParse::BadRequest, ParseOnly(Get("/wads/a.wad")));
	EXPECT_EQ(HttpParse::BadRequest, ParseOnly(Get("/../secrets.cfg")));
	EXPECT_EQ(HttpParse::BadRequest, ParseOnly(Get("//etc/passwd")));
}

TEST(ParseHttpRequest, RefusesASeparatorSmuggledThroughTheEncoding)
{
	// The reason decoding happens after the segment split rather than before it.
	EXPECT_EQ(HttpParse::BadRequest, ParseOnly(Get("/%2e%2e%2fsecrets.cfg")));
	EXPECT_EQ(HttpParse::BadRequest, ParseOnly(Get("/a%2Fb.wad")));
	EXPECT_EQ(HttpParse::BadRequest, ParseOnly(Get("/a%5Cb.wad"))) << "backslash separates too";
}

TEST(ParseHttpRequest, RefusesControlCharactersInTheName)
{
	EXPECT_EQ(HttpParse::BadRequest, ParseOnly(Get("/a%00.wad"))) << "NUL truncates C strings";
	EXPECT_EQ(HttpParse::BadRequest, ParseOnly(Get("/a%0a.wad"))) << "newline forges log lines";
	EXPECT_EQ(HttpParse::BadRequest, ParseOnly(Get("/a%7f.wad")));
}

TEST(ParseHttpRequest, RefusesABrokenEscapeInTheTarget)
{
	EXPECT_EQ(HttpParse::BadRequest, ParseOnly(Get("/a%2.wad")));
}
