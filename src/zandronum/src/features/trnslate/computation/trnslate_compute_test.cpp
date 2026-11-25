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

TEST(TrnslateSetTranslation, EmptyResetsKnownAppliesUnknownKeeps)
{
	EXPECT_EQ(ComputeSetTranslationOutcome(true, false), kSetTranslationDefault);
	EXPECT_EQ(ComputeSetTranslationOutcome(true, true), kSetTranslationDefault);
	EXPECT_EQ(ComputeSetTranslationOutcome(false, true), kSetTranslationFound);
	EXPECT_EQ(ComputeSetTranslationOutcome(false, false), kSetTranslationKeep);
}
