// [Doruk-Aksoy] Tests for the TRNSLATE parser, the palette lump reader and the named-translation helpers.
#include "gtest/gtest.h"
#include "features/trnslate/computation/trnslate_compute.h"

#include <cstring>
#include <string>
#include <vector>

using namespace zx::trnslate;

namespace {

TrnslateParse Parse(const std::string &text, const char *paletteLump = nullptr)
{
	return ComputeParseTrnslate(text.data(), text.size(), [paletteLump](const std::string &s) {
		return paletteLump != nullptr && s == paletteLump;
	});
}

void ExpectError(const std::string &text, const std::string &error, int line, const char *paletteLump = nullptr)
{
	const TrnslateParse p = Parse(text, paletteLump);
	EXPECT_EQ(p.error, error) << text;
	EXPECT_EQ(p.errorLine, line) << text;
}

std::vector<unsigned char> Bytes(const std::string &s)
{
	return std::vector<unsigned char>(s.begin(), s.end());
}

void AppendChunk(std::vector<unsigned char> &png, const char *id, const std::vector<unsigned char> &data, unsigned declared)
{
	png.push_back((unsigned char)(declared >> 24));
	png.push_back((unsigned char)(declared >> 16));
	png.push_back((unsigned char)(declared >> 8));
	png.push_back((unsigned char)declared);
	png.insert(png.end(), id, id + 4);
	png.insert(png.end(), data.begin(), data.end());
	png.insert(png.end(), 4, 0);
}

std::vector<unsigned char> PngStart()
{
	const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	std::vector<unsigned char> png(sig, sig + 8);
	AppendChunk(png, "IHDR", std::vector<unsigned char>(13, 1), 13);
	return png;
}

int ReadPalette(const std::vector<unsigned char> &lump, unsigned char *out, std::string *error)
{
	return ComputeReadPaletteLump(lump.data(), lump.size(), out, error);
}

} // namespace

TEST(Trnslate, EmptyLumpHasNoDefinitions)
{
	const TrnslateParse p = Parse("  \n// only a comment");
	EXPECT_TRUE(p.defs.empty());
	EXPECT_TRUE(p.error.empty());
}

TEST(Trnslate, ParsesEachBaseKindWithItsEntriesAndLine)
{
	const TrnslateParse p = Parse(
		"// UZDoom's wiki example\n"
		"BlueBaron = \"112:127=192:207\", \"16:31=200:207\"\n"
		"/* a block *\n comment */ GreenGuy : 2 = \"112:127=[0,255,0]:[0,0,0]\"\n"
		"_Copy1:BlueBaron=\"0:0=0:0\"");
	ASSERT_TRUE(p.error.empty()) << p.error;
	ASSERT_EQ(p.defs.size(), 3u);

	EXPECT_EQ(p.defs[0].name, "BlueBaron");
	EXPECT_EQ(p.defs[0].baseKind, kBaseIdentity);
	EXPECT_EQ(p.defs[0].line, 2);
	ASSERT_EQ(p.defs[0].entries.size(), 2u);
	EXPECT_EQ(p.defs[0].entries[0].text, "112:127=192:207");
	EXPECT_FALSE(p.defs[0].entries[0].palette);
	EXPECT_EQ(p.defs[0].entries[1].text, "16:31=200:207");

	EXPECT_EQ(p.defs[1].name, "GreenGuy");
	EXPECT_EQ(p.defs[1].baseKind, kBaseStandard);
	EXPECT_EQ(p.defs[1].baseStandard, 2);
	EXPECT_EQ(p.defs[1].line, 4);

	EXPECT_EQ(p.defs[2].name, "_Copy1");
	EXPECT_EQ(p.defs[2].baseKind, kBaseNamed);
	EXPECT_EQ(p.defs[2].baseName, "BlueBaron");
	EXPECT_EQ(p.defs[2].line, 5);
}

TEST(Trnslate, StandardBaseTakesZeroToSix)
{
	EXPECT_TRUE(Parse("A : 0 = \"0:0=0:0\"").error.empty());
	EXPECT_TRUE(Parse("A : 6 = \"0:0=0:0\"").error.empty());
	ExpectError("A : 7 = \"0:0=0:0\"", "Translation must be in the range [0,6]", 1);
	ExpectError("A : -1 = \"0:0=0:0\"", "Expected a base translation but got '-' instead", 1);
	ExpectError("A : 1.5 = \"0:0=0:0\"", "Expected a base translation but got '1.5' instead", 1);
	ExpectError("A : 2. = \"0:0=0:0\"", "Expected a base translation but got '2.' instead", 1);
}

TEST(Trnslate, PaletteLumpTakesAnOptionalStart)
{
	const TrnslateParse p = Parse("A = \"MYPAL\", 16, \"1:2=3:4\", \"MYPAL\", -3, \"MYPAL\", 0x10\nB = \"MYPAL\"\nC = \"5:6=7:8\"", "MYPAL");
	ASSERT_TRUE(p.error.empty()) << p.error;
	ASSERT_EQ(p.defs.size(), 3u);
	ASSERT_EQ(p.defs[0].entries.size(), 4u);
	EXPECT_TRUE(p.defs[0].entries[0].palette);
	EXPECT_EQ(p.defs[0].entries[0].start, 16);
	EXPECT_FALSE(p.defs[0].entries[1].palette);
	EXPECT_EQ(p.defs[0].entries[2].start, -3);
	EXPECT_EQ(p.defs[0].entries[3].start, 16);
	ASSERT_EQ(p.defs[1].entries.size(), 1u);
	EXPECT_TRUE(p.defs[1].entries[0].palette);
	EXPECT_EQ(p.defs[1].entries[0].start, 0);
	EXPECT_FALSE(p.defs[2].entries[0].palette);
}

TEST(Trnslate, ACommaAfterAPaletteLumpTakesItsStart)
{
	// [Doruk-Aksoy] UZDoom's MustGetValue after the comma: a range cannot follow a palette lump directly.
	ExpectError("A = \"P\", \"5:6=7:8\"", "Expected a number but got '5:6=7:8' instead", 1, "P");
}

TEST(Trnslate, ACommaAfterARangeIsNotAStart)
{
	ExpectError("A = \"1:2=3:4\", 16", "Expected a string but got '16' instead", 1);
}

TEST(Trnslate, PaletteStartMustBeANumber)
{
	ExpectError("A = \"P\", \"x\"", "Expected a number but got 'x' instead", 1, "P");
	ExpectError("A = \"P\", - \"x\"", "Expected a number but got 'x' instead", 1, "P");
	ExpectError("A = \"P\", 0x", "Bad numeric constant '0x'", 1, "P");
	ExpectError("A = \"P\", 0X", "Bad numeric constant '0X'", 1, "P");
	ExpectError("A = \"P\", 0xg", "Bad numeric constant '0xg'", 1, "P");
	ExpectError("A = \"P\",", "Expected a number but reached the end of the lump", 1, "P");
}

TEST(Trnslate, SyntaxErrorsNameTheirLine)
{
	ExpectError("\n\"A\" = \"0:0=0:0\"", "Expected a translation name but got 'A' instead", 2);
	ExpectError("A \"0:0=0:0\"", "Expected '=' but got '0:0=0:0' instead", 1);
	ExpectError("A / \"0:0=0:0\"", "Expected '=' but got '/' instead", 1);
	ExpectError("A", "Expected '=' but reached the end of the lump", 1);
	ExpectError("A =\n\n5", "Expected a string but got '5' instead", 3);
	ExpectError("A = \"0:0=0:0\",", "Expected a string but reached the end of the lump", 1);
	ExpectError("A = \"0:0=0:0\" /", "Expected a translation name but got '/' instead", 1);
	ExpectError("A : 09 = \"0:0=0:0\"", "Bad numeric constant '09'", 1);
	ExpectError("A : 1_2 = \"0:0=0:0\"", "Bad numeric constant '1_2'", 1);
}

TEST(Trnslate, LexErrorsStopTheParseWhereTheyAre)
{
	ExpectError("A = \"0:0=0:0\"\n/* never closed\n", "Unterminated comment", 2);
	ExpectError("A = \"0:0=0:0\" /*", "Unterminated comment", 1);
	ExpectError("A = \"never closed", "Unterminated string", 1);
	ExpectError("A : \"never closed", "Unterminated string", 1);
	ExpectError("A \"never closed", "Unterminated string", 1);
	ExpectError("A = \"0:0=0:0\", \"ends in a backslash\\", "Unterminated string", 1);
}

TEST(Trnslate, DefinitionsBeforeAnErrorAreKept)
{
	const TrnslateParse p = Parse("A = \"1:1=2:2\"\nB = 5");
	ASSERT_EQ(p.defs.size(), 1u);
	EXPECT_EQ(p.defs[0].name, "A");
	EXPECT_EQ(p.errorLine, 2);
}

TEST(Trnslate, StringsUnescapeAndCountTheirLines)
{
	const TrnslateParse p = Parse("A = \"a\\\"b\\\\c\\nd\\qe\", \"two\nlines\", \"joined\\\nline\"\nB = \"x\"");
	ASSERT_TRUE(p.error.empty()) << p.error;
	ASSERT_EQ(p.defs.size(), 2u);
	EXPECT_EQ(p.defs[0].entries[0].text, "a\"b\\c\nd\\qe");
	EXPECT_EQ(p.defs[0].entries[1].text, "two\nlines");
	EXPECT_EQ(p.defs[0].entries[2].text, "joined\\\nline");
	EXPECT_EQ(p.defs[1].line, 4);
}

TEST(Trnslate, BuiltinNamesIgnoreCase)
{
	int player = -1;
	EXPECT_EQ(ComputeTrnslateBuiltin("None", &player), kBuiltinNone);
	EXPECT_EQ(ComputeTrnslateBuiltin("nOnE", &player), kBuiltinNone);
	EXPECT_EQ(ComputeTrnslateBuiltin("Ice", &player), kBuiltinIce);
	EXPECT_EQ(ComputeTrnslateBuiltin("ICE", &player), kBuiltinIce);
	EXPECT_EQ(ComputeTrnslateBuiltin("Player1", &player), kBuiltinPlayer);
	EXPECT_EQ(player, 0);
	EXPECT_EQ(ComputeTrnslateBuiltin("player8", &player), kBuiltinPlayer);
	EXPECT_EQ(player, 7);
}

TEST(Trnslate, OtherNamesAreNotBuiltin)
{
	int player = -1;
	const char *names[] = { "", "Ic", "Ices", "Player0", "Player9", "Player10", "Players", "Playe", "Xlayer1", "BlueBaron" };
	for (const char *name : names) EXPECT_EQ(ComputeTrnslateBuiltin(name, &player), kNotBuiltin) << name;
	EXPECT_EQ(player, -1);
}

TEST(TrnslatePalette, RawLumpIsReadAsAFullPalette)
{
	std::vector<unsigned char> raw(800);
	for (size_t i = 0; i < raw.size(); i++) raw[i] = (unsigned char)i;
	unsigned char out[768];
	std::string error;
	EXPECT_EQ(ReadPalette(raw, out, &error), 256);
	EXPECT_EQ(out[767], 767 & 0xFF);

	const std::vector<unsigned char> shortRaw = { 1, 2, 3, 4, 5, 6 };
	EXPECT_EQ(ReadPalette(shortRaw, out, &error), 256);
	EXPECT_EQ(out[5], 6);
	EXPECT_EQ(out[6], 0);

	EXPECT_EQ(ReadPalette({}, out, &error), 256);
	EXPECT_EQ(out[0], 0);
	EXPECT_TRUE(error.empty());
}

TEST(TrnslatePalette, PngGivesItsPlteColours)
{
	std::vector<unsigned char> png = PngStart();
	AppendChunk(png, "tEXt", Bytes("hello"), 5);
	AppendChunk(png, "PLTE", { 10, 20, 30, 40, 50, 60 }, 6);
	AppendChunk(png, "IDAT", {}, 0);
	unsigned char out[768];
	std::string error;
	EXPECT_EQ(ReadPalette(png, out, &error), 2);
	EXPECT_EQ(out[0], 10);
	EXPECT_EQ(out[5], 60);
	EXPECT_EQ(out[6], 0);
}

TEST(TrnslatePalette, PngPlteIsCutToWhatItHolds)
{
	std::vector<unsigned char> big = PngStart();
	AppendChunk(big, "PLTE", std::vector<unsigned char>(800, 7), 800);
	unsigned char out[768];
	std::string error;
	EXPECT_EQ(ReadPalette(big, out, &error), 256);
	EXPECT_EQ(out[767], 7);

	std::vector<unsigned char> truncated = PngStart();
	AppendChunk(truncated, "PLTE", {}, 9);
	truncated.insert(truncated.end() - 4, { 1, 2, 3, 4, 5, 6 });
	truncated.resize(truncated.size() - 4);
	EXPECT_EQ(ReadPalette(truncated, out, &error), 2);
}

TEST(TrnslatePalette, PngWithoutPlteIsAnError)
{
	unsigned char out[768];
	const char *ends[] = { "IDAT", "IEND" };
	for (const char *end : ends)
	{
		std::vector<unsigned char> png = PngStart();
		AppendChunk(png, end, {}, 0);
		AppendChunk(png, "PLTE", { 1, 2, 3 }, 3);
		std::string error;
		EXPECT_EQ(ReadPalette(png, out, &error), -1) << end;
		EXPECT_EQ(error, "contains no palette");
	}

	std::vector<unsigned char> overlong = PngStart();
	AppendChunk(overlong, "tEXt", {}, 1000);
	std::string error;
	EXPECT_EQ(ReadPalette(overlong, out, &error), -1);

	std::vector<unsigned char> signatureOnly = PngStart();
	signatureOnly.resize(12);
	EXPECT_EQ(ReadPalette(signatureOnly, out, &error), -1);
}

TEST(TrnslatePalette, JascGivesItsColours)
{
	unsigned char out[768];
	std::string error;
	EXPECT_EQ(ReadPalette(Bytes("JASC-PAL\r\n0100\r\n2\r\n255 0 0\r\n0 255 0\r\n"), out, &error), 2);
	EXPECT_EQ(out[0], 255);
	EXPECT_EQ(out[4], 255);
	EXPECT_EQ(out[6], 0);
	EXPECT_EQ(ReadPalette(Bytes("JASC-PAL\n0100\n0\n"), out, &error), 0);
	EXPECT_EQ(ReadPalette(Bytes("JASC-PAL\n0100\n-4\n"), out, &error), 0);
	EXPECT_EQ(ReadPalette(Bytes("JASC-PAL\n0100\n+0X1\n0xA 0Xb +0x0\n"), out, &error), 1);
	EXPECT_EQ(out[0], 10);
	EXPECT_EQ(out[1], 11);
	EXPECT_EQ(out[2], 0);

	std::string many = "JASC-PAL\n0100\n300\n";
	for (int i = 0; i < 768; i++) many += "9 ";
	EXPECT_EQ(ReadPalette(Bytes(many), out, &error), 256);
	EXPECT_EQ(out[767], 9);
	EXPECT_TRUE(error.empty());
}

TEST(TrnslatePalette, BadJascIsAnError)
{
	unsigned char out[768];
	struct Case { const char *lump; const char *error; };
	const Case cases[] = {
		{ "JASC-PAL\n0100\n1\n255 256 0", "Color 256 value out of range" },
		{ "JASC-PAL\n0100\n1\n255 -1 0", "Color -1 value out of range" },
		{ "JASC-PAL\n0100\n1\n255 red 0", "Bad numeric constant 'red'" },
		{ "JASC-PAL\n0100\n1\n255 -0x 0", "Bad numeric constant '-0x'" },
		{ "JASC-PAL\n0100\n1\n255 +0X 0", "Bad numeric constant '+0X'" },
		{ "JASC-PAL\n0100\n1\n255 0xg 0", "Bad numeric constant '0xg'" },
		{ "JASC-PAL\n0100\n2\n255 0 0", "Missing integer (unexpected end of file)" },
		{ "JASC-PAL\n0100\n", "Missing integer (unexpected end of file)" },
	};
	for (const Case &c : cases)
	{
		std::string error;
		EXPECT_EQ(ReadPalette(Bytes(c.lump), out, &error), -1) << c.lump;
		EXPECT_EQ(error, c.error);
	}
}

TEST(TrnslatePalette, SpanMustStayInsideTheTable)
{
	EXPECT_TRUE(ComputePaletteSpanFits(0, 256));
	EXPECT_TRUE(ComputePaletteSpanFits(16, 240));
	EXPECT_TRUE(ComputePaletteSpanFits(255, 1));
	EXPECT_FALSE(ComputePaletteSpanFits(0, 257));
	EXPECT_FALSE(ComputePaletteSpanFits(-1, 1));
	EXPECT_FALSE(ComputePaletteSpanFits(256, 1));
	EXPECT_FALSE(ComputePaletteSpanFits(0, 0));
}

TEST(TrnslateSetTranslation, ResetRestoresKnownAppliesUnknownKeeps)
{
	EXPECT_EQ(ComputeSetTranslationOutcome(true, false), kSetTranslationDefault);
	EXPECT_EQ(ComputeSetTranslationOutcome(true, true), kSetTranslationDefault);
	EXPECT_EQ(ComputeSetTranslationOutcome(false, true), kSetTranslationFound);
	EXPECT_EQ(ComputeSetTranslationOutcome(false, false), kSetTranslationKeep);
}

// [Doruk-Aksoy] UZDoom's AddColourisation, worked out by hand: the target times (.299 r + .587 g + .114 b) / 255, each channel's low byte kept.
TEST(TrnslateRanges, ColouriseScalesTheTargetByLuminance)
{
	const auto same = [](RangeColor c, int r, int g, int b) { return c.r == r && c.g == g && c.b == b; };
	EXPECT_TRUE(same(ComputeColourise(255, 255, 255, 200, 100, 50), 200, 100, 50));
	EXPECT_TRUE(same(ComputeColourise(100, 0, 0, 200, 100, 50), 23, 11, 5));
	EXPECT_TRUE(same(ComputeColourise(0, 0, 0, 200, 100, 50), 0, 0, 0));
	EXPECT_TRUE(same(ComputeColourise(64, 128, 192, 255, 255, 255), 116, 116, 116));
	// [Doruk-Aksoy] Past a byte, as UZDoom's PalEntry keeps it: 300 is 44, -50 is 206, 1000 is 232.
	EXPECT_TRUE(same(ComputeColourise(255, 255, 255, 300, -50, 1000), 44, 206, 232));
	// [Doruk-Aksoy] A value too big for an int is held at its edge rather than overflowing the cast.
	EXPECT_TRUE(same(ComputeColourise(255, 255, 255, 2147483647, -2147483647 - 1, 0), 255, 0, 0));
}

// [Doruk-Aksoy] UZDoom's AddTint: amount percent of the way from the colour to the target, in float.
TEST(TrnslateRanges, TintMovesTowardsTheTarget)
{
	const auto same = [](RangeColor c, int r, int g, int b) { return c.r == r && c.g == g && c.b == b; };
	EXPECT_TRUE(same(ComputeTint(100, 50, 0, 200, 100, 50, 50), 150, 75, 25));
	EXPECT_TRUE(same(ComputeTint(100, 50, 0, 200, 100, 50, 25), 125, 62, 12));
	EXPECT_TRUE(same(ComputeTint(255, 255, 255, 0, 0, 0, 33), 170, 170, 170));
	EXPECT_TRUE(same(ComputeTint(100, 50, 0, 200, 100, 50, 0), 100, 50, 0));
	// [Doruk-Aksoy] UZDoom clamps neither the amount nor the target, so the result wraps.
	EXPECT_TRUE(same(ComputeTint(0, 0, 0, 200, 0, 0, 200), 144, 0, 0));
	EXPECT_TRUE(same(ComputeTint(100, 0, 0, 0, 0, 0, -100), 200, 0, 0));
	EXPECT_TRUE(same(ComputeTint(10, 10, 10, -50, 0, 0, 100), 206, 0, 0));
}

TEST(TrnslateRanges, OnlyThe256PaletteEntriesFit)
{
	EXPECT_TRUE(ComputePaletteIndexFits(0));
	EXPECT_TRUE(ComputePaletteIndexFits(255));
	EXPECT_FALSE(ComputePaletteIndexFits(-1));
	EXPECT_FALSE(ComputePaletteIndexFits(256));
}

TEST(TrnslateRanges, ARangeFitsWhenEveryIndexItNamesDoes)
{
	TranslationRange range;
	range.kind = kRangeColourise;
	range.start = 0; range.end = 255;
	EXPECT_TRUE(ComputeTranslationRangeFits(range));
	range.end = 300;
	EXPECT_FALSE(ComputeTranslationRangeFits(range));
	range.end = 10; range.start = -1;
	EXPECT_FALSE(ComputeTranslationRangeFits(range));
	// [Doruk-Aksoy] A palette range's targets are indices too; another kind's colours aren't.
	range.start = 0; range.pal2 = 256;
	EXPECT_TRUE(ComputeTranslationRangeFits(range));
	range.kind = kRangePalette;
	EXPECT_FALSE(ComputeTranslationRangeFits(range));
	range.pal2 = 0; range.pal1 = -1;
	EXPECT_FALSE(ComputeTranslationRangeFits(range));
	range.pal1 = 255;
	EXPECT_TRUE(ComputeTranslationRangeFits(range));
}

namespace {

// [Doruk-Aksoy] What a range's write did, field by field: b byte, s short-byte, l long, f float.
struct Ops
{
	std::string kinds;
	std::vector<double> values;
	size_t next = 0;
	void shortByte(int v, int bits) { kinds += 's'; values.push_back(v); values.push_back(bits); }
	void byte(int v) { kinds += 'b'; values.push_back(v); }
	void longValue(int v) { kinds += 'l'; values.push_back(v); }
	void floatValue(float v) { kinds += 'f'; values.push_back(v); }
	// [Doruk-Aksoy] The same fields read back in order, the short-byte's width checked.
	int shortByte(int bits) { const int v = int(values[next]); EXPECT_EQ(bits, int(values[next + 1])); next += 2; return v; }
	int byte() { return int(values[next++]); }
	int longValue() { return int(values[next++]); }
	float floatValue() { return float(values[next++]); }
};

TranslationRange RangeOf(int kind)
{
	TranslationRange r;
	r.kind = kind;
	r.start = 16; r.end = 31;
	r.pal1 = 112; r.pal2 = 127;
	r.r1 = 255; r.g1 = 128; r.b1 = 7; r.r2 = 1; r.g2 = 2; r.b2 = 3;
	r.fr1 = 0.5f; r.fg1 = 1.25f; r.fb1 = 2.0f; r.fr2 = 0.0f; r.fg2 = 0.75f; r.fb2 = 1.5f;
	r.amount = 40;
	return r;
}

} // namespace

TEST(TrnslateRanges, EachKindWritesItsOwnFields)
{
	// [Doruk-Aksoy] The first three as the server always sent them; colourise and tint carry whole ints, as UZDoom applies them unclamped.
	const struct { int kind; const char *ops; } cases[] = {
		{ kRangePalette, "sbbbb" }, { kRangeRgb, "sbbbbbbbb" }, { kRangeDesaturated, "sbbffffff" },
		{ kRangeColourise, "sbblll" }, { kRangeTint, "sbbllll" },
	};
	for (const auto &c : cases)
	{
		Ops ops;
		ComputeWriteTranslationRange(ops, RangeOf(c.kind));
		EXPECT_EQ(c.ops, ops.kinds) << c.kind;
		EXPECT_EQ(c.kind, int(ops.values[0]));
		EXPECT_EQ(7, int(ops.values[1]));
	}
	Ops tint;
	ComputeWriteTranslationRange(tint, RangeOf(kRangeTint));
	EXPECT_EQ((std::vector<double>{ 4, 7, 16, 31, 40, 255, 128, 7 }), tint.values);
}

TEST(TrnslateRanges, EachKindReadsBackWhatWasWritten)
{
	for (int kind = kRangePalette; kind < kRangeKinds; kind++)
	{
		Ops ops;
		const TranslationRange sent = RangeOf(kind);
		ComputeWriteTranslationRange(ops, sent);
		TranslationRange got;
		ASSERT_TRUE(ComputeReadTranslationRange(ops, got)) << kind;
		EXPECT_EQ(ops.values.size(), ops.next) << kind;
		EXPECT_EQ(sent.kind, got.kind);
		EXPECT_EQ(sent.start, got.start);
		EXPECT_EQ(sent.end, got.end);
		if (kind == kRangePalette) { EXPECT_EQ(sent.pal1, got.pal1); EXPECT_EQ(sent.pal2, got.pal2); }
		if (kind == kRangeRgb) { EXPECT_EQ(sent.r2, got.r2); EXPECT_EQ(sent.g2, got.g2); EXPECT_EQ(sent.b2, got.b2); }
		if (kind == kRangeDesaturated) { EXPECT_EQ(sent.fr1, got.fr1); EXPECT_EQ(sent.fg2, got.fg2); EXPECT_EQ(sent.fb2, got.fb2); }
		if (kind != kRangePalette && kind != kRangeDesaturated) { EXPECT_EQ(sent.r1, got.r1); EXPECT_EQ(sent.g1, got.g1); EXPECT_EQ(sent.b1, got.b1); }
		EXPECT_EQ(kind == kRangeTint ? sent.amount : 0, got.amount);
	}
}

TEST(TrnslateRanges, AnUnknownKindWritesOnlyItsKindAndIsNotRead)
{
	// [Doruk-Aksoy] The client read any kind past the first two as a desaturation's six floats.
	for (int kind : { -1, int(kRangeKinds), 127 })
	{
		Ops ops;
		TranslationRange range = RangeOf(kRangePalette);
		range.kind = kind;
		ComputeWriteTranslationRange(ops, range);
		EXPECT_EQ("s", ops.kinds) << kind;
		TranslationRange got;
		EXPECT_FALSE(ComputeReadTranslationRange(ops, got)) << kind;
		EXPECT_EQ(2u, ops.next);
	}
}
