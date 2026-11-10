#include "languageparse_compute.h"

#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>

using namespace zx::language;

namespace {

std::string Escaped(const char *text)
{
	std::string buffer(text);
	buffer.push_back('\0');
	const size_t len = ComputeLanguageEscapes(&buffer[0]);
	return std::string(buffer.c_str(), len);
}

std::vector<std::vector<std::string>> Csv(const char *text)
{
	return ComputeParseLanguageCSV(text, strlen(text));
}

const uint32_t kEnu = ComputeLanguageCode('e', 'n', 'u');
const uint32_t kEng = ComputeLanguageCode('e', 'n', 'g');
const uint32_t kEn = ComputeLanguageCode('e', 'n', 0);
const uint32_t kFr = ComputeLanguageCode('f', 'r', 0);

} // namespace

TEST(LanguageEscapes, NamedEscapes)
{
	EXPECT_EQ(Escaped("a\\nb\\rc\\td"), "a\nb\rc\td");
	EXPECT_EQ(Escaped("\\cGred"), std::string(1, kLanguageColorEscape) + "Gred");
	EXPECT_EQ(Escaped("\\\"q\\\\"), "\"q\\");
}

TEST(LanguageEscapes, BackslashNewlineJoinsLines)
{
	EXPECT_EQ(Escaped("one\\\ntwo"), "onetwo");
}

TEST(LanguageEscapes, HexEscapes)
{
	EXPECT_EQ(Escaped("\\x41\\x62"), "Ab");
	EXPECT_EQ(Escaped("\\xfF"), "\xff");
	EXPECT_EQ(Escaped("\\x4g"), "\x04g");
	EXPECT_EQ(Escaped("\\xAz"), "\x0az");
}

TEST(LanguageEscapes, ZeroHexEscapeIsDropped)
{
	EXPECT_EQ(Escaped("a\\x00b"), "ab");
	EXPECT_EQ(Escaped("a\\xZb"), "aZb");
	EXPECT_EQ(Escaped("a\\x"), "a");
}

TEST(LanguageEscapes, TrailingBackslashEndsTheString)
{
	// [Doruk-Aksoy] Upstream wrote the terminator and read on past it; under ASan this read was out of bounds.
	char buffer[] = { 'a', 'b', '\\', '\0', 'X', 'Y', '\0' };
	EXPECT_EQ(ComputeLanguageEscapes(buffer), 2u);
	EXPECT_STREQ(buffer, "ab");
}

TEST(LanguageCSV, Detection)
{
	EXPECT_FALSE(ComputeIsLanguageCSV("default,", 8));
	EXPECT_TRUE(ComputeIsLanguageCSV("default,fr\nX", 12));
	EXPECT_TRUE(ComputeIsLanguageCSV("IDENTIFIER,enu", 14));
	EXPECT_FALSE(ComputeIsLanguageCSV("[enu default]\n", 14));
}

TEST(LanguageCSV, CellsQuotesAndRows)
{
	const auto rows = Csv("a,\"b,c\",\"say \"\"hi\"\"\"\r\nd\"e,\"two\nlines\"\nlast");
	ASSERT_EQ(rows.size(), 3u);
	EXPECT_EQ(rows[0], (std::vector<std::string>{ "a", "b,c", "say \"hi\"" }));
	EXPECT_EQ(rows[1], (std::vector<std::string>{ "de", "two\nlines" }));
	EXPECT_EQ(rows[2], (std::vector<std::string>{ "last" }));
}

TEST(LanguageCSV, CellsTakeEscapesAndNoTrailingRow)
{
	const auto rows = Csv("x\\x41,\\n\n,\n");
	ASSERT_EQ(rows.size(), 2u);
	EXPECT_EQ(rows[0], (std::vector<std::string>{ "xA", "\n" }));
	EXPECT_EQ(rows[1], (std::vector<std::string>{ "", "" }));
	EXPECT_TRUE(Csv("").empty());
}

TEST(LanguageHeader, CodesAreLowercased)
{
	const LanguageHeader header = ComputeLanguageHeader({ "PTB", "pt", "br" });
	EXPECT_EQ(header.codes, (std::vector<uint32_t>{ ComputeLanguageCode('p', 't', 'b'), ComputeLanguageCode('p', 't', 0), ComputeLanguageCode('b', 'r', 0) }));
	EXPECT_FALSE(header.isDefault);
	EXPECT_FALSE(header.tilde);
	EXPECT_FALSE(header.emptyCode);
}

TEST(LanguageHeader, DefaultAndGlobalKeepTheirTable)
{
	const LanguageHeader enuDefault = ComputeLanguageHeader({ "enu", "Default" });
	EXPECT_EQ(enuDefault.codes, (std::vector<uint32_t>{ kLanguageDefault }));
	EXPECT_TRUE(enuDefault.isDefault);

	EXPECT_EQ(ComputeLanguageHeader({ "default", "fr" }).codes, (std::vector<uint32_t>{ kLanguageDefault }));
	EXPECT_EQ(ComputeLanguageHeader({ "*", "fr" }).codes, (std::vector<uint32_t>{ kLanguageGlobal }));
	EXPECT_EQ(ComputeLanguageHeader({ "fr", "*" }).codes, (std::vector<uint32_t>{ kLanguageGlobal }));
}

TEST(LanguageHeader, TildeEmptyAndUnknownCodes)
{
	const LanguageHeader header = ComputeLanguageHeader({ "~", "fr", "Hans", "x", "" });
	EXPECT_EQ(header.codes, (std::vector<uint32_t>{ kFr }));
	EXPECT_TRUE(header.tilde);
	EXPECT_TRUE(header.emptyCode);
}

TEST(LanguageHeader, Matching)
{
	const LanguageHeader header = ComputeLanguageHeader({ "enu", "fr" });
	EXPECT_TRUE(ComputeLanguageHeaderMatches(header, kEnu, true));
	EXPECT_FALSE(ComputeLanguageHeaderMatches(header, kEng, true));
	EXPECT_TRUE(ComputeLanguageHeaderMatches(header, kEng, false));
	EXPECT_FALSE(ComputeLanguageHeaderMatches(header, kEn, true));
	EXPECT_TRUE(ComputeLanguageHeaderMatches(ComputeLanguageHeader({ "en" }), kEn, true));
	EXPECT_FALSE(ComputeLanguageHeaderMatches(LanguageHeader(), kEnu, false));
}

TEST(LanguagePasses, DefaultOnly)
{
	const uint32_t ids[4] = { kEnu, kEnu, kEnu, kEnu };
	const auto passes = ComputeLanguagePasses(ids, true);
	ASSERT_EQ(passes.size(), 1u);
	EXPECT_EQ(passes[0].code, kLanguageDefault);
	EXPECT_TRUE(passes[0].exact);
}

TEST(LanguagePasses, EveryLanguageThenDefault)
{
	const uint32_t ids[4] = { kEnu, kFr, kEnu, kFr };
	const auto passes = ComputeLanguagePasses(ids, false);
	ASSERT_EQ(passes.size(), 14u);
	EXPECT_EQ(passes[0].code, kLanguageGlobal);
	EXPECT_EQ(passes[1].code, kEnu);
	EXPECT_TRUE(passes[1].exact);
	EXPECT_EQ(passes[2].code, kEn);
	EXPECT_TRUE(passes[2].exact);
	EXPECT_EQ(passes[3].code, kEnu);
	EXPECT_FALSE(passes[3].exact);
	EXPECT_EQ(passes[4].code, kFr);
	EXPECT_EQ(passes[13].code, kLanguageDefault);
}

TEST(LanguageFilter, Games)
{
	EXPECT_TRUE(ComputeLanguageFilterAllows("", "Doom"));
	EXPECT_TRUE(ComputeLanguageFilterAllows("   ", nullptr));
	EXPECT_TRUE(ComputeLanguageFilterAllows("heretic  doom", "Doom"));
	EXPECT_FALSE(ComputeLanguageFilterAllows("heretic hexen", "Doom"));
	EXPECT_FALSE(ComputeLanguageFilterAllows("doom", nullptr));
}

TEST(LanguageCSVLayout, Columns)
{
	const LanguageCSVLayout layout = ComputeLanguageCSVLayout({ "default", "Identifier", "Remarks", "FILTER", "fr ptb", "~", "" });
	EXPECT_EQ(layout.labelColumn, 1);
	EXPECT_EQ(layout.filterColumn, 3);
	EXPECT_TRUE(layout.isDefault);
	ASSERT_EQ(layout.languages.size(), 3u);
	EXPECT_EQ(layout.languages[0].column, 0u);
	EXPECT_EQ(layout.languages[0].header.codes, (std::vector<uint32_t>{ kLanguageDefault }));
	EXPECT_EQ(layout.languages[1].column, 4u);
	EXPECT_EQ(layout.languages[1].header.codes, (std::vector<uint32_t>{ kFr }));
	EXPECT_EQ(layout.languages[2].column, 4u);
}

TEST(LanguageCSVOps, NoLabelColumnSetsNothing)
{
	// [Doruk-Aksoy] Upstream indexed every row at column -1 here.
	const auto rows = Csv("default,fr\nhello,salut\n");
	EXPECT_TRUE(ComputeLanguageCSVOps(rows, ComputeLanguageCSVLayout(rows[0]), kFr, true, "Doom").empty());
}

TEST(LanguageCSVOps, RowsForOnePass)
{
	const auto rows = Csv(
		"identifier,filter,fr,enu\n"
		" GREET ,,salut,hello\n"
		"HERETIC,heretic,hi,hi\n"
		"EMPTY,,,x\n"
		",,lost,lost\n"
		"SHORT\n"
		"CUT,,only\n"
		"\"\"\n");
	const LanguageCSVLayout layout = ComputeLanguageCSVLayout(rows[0]);
	EXPECT_FALSE(layout.isDefault);

	const auto ops = ComputeLanguageCSVOps(rows, layout, kFr, true, "Doom");
	ASSERT_EQ(ops.size(), 3u);
	EXPECT_EQ(ops[0].kind, LanguageCSVOp::Set);
	EXPECT_EQ(ops[0].label, "GREET");
	EXPECT_EQ(ops[0].text, "salut");
	EXPECT_EQ(ops[1].kind, LanguageCSVOp::Remove);
	EXPECT_EQ(ops[1].label, "EMPTY");
	EXPECT_EQ(ops[2].label, "CUT");
	EXPECT_EQ(ops[2].text, "only");

	const auto enu = ComputeLanguageCSVOps(rows, layout, kEnu, true, "Heretic");
	ASSERT_EQ(enu.size(), 3u);
	EXPECT_EQ(enu[1].label, "HERETIC");
}

TEST(LanguageCSVOps, DefaultCSVPurgesOlderText)
{
	// [Doruk-Aksoy] The 'lonely' row ends before the identifier column, so it names no label.
	const auto rows = Csv("default,identifier\nnew,LABEL\nlonely\n,BLANK\n");
	const LanguageCSVLayout layout = ComputeLanguageCSVLayout(rows[0]);
	const auto ops = ComputeLanguageCSVOps(rows, layout, kFr, true, "Doom");
	ASSERT_EQ(ops.size(), 2u);
	EXPECT_EQ(ops[0].kind, LanguageCSVOp::PurgeOlder);
	EXPECT_EQ(ops[0].label, "LABEL");
	EXPECT_EQ(ops[1].kind, LanguageCSVOp::PurgeOlder);
	EXPECT_EQ(ops[1].label, "BLANK");

	const auto def = ComputeLanguageCSVOps(rows, layout, kLanguageDefault, true, "Doom");
	ASSERT_EQ(def.size(), 4u);
	EXPECT_EQ(def[1].kind, LanguageCSVOp::Set);
	EXPECT_EQ(def[1].text, "new");
	EXPECT_EQ(def[3].kind, LanguageCSVOp::Remove);
}

TEST(LanguageNames, CompareIgnoresCase)
{
	EXPECT_EQ(ComputeLanguageCompareNoCase("Hello", "hELLO"), 0);
	EXPECT_LT(ComputeLanguageCompareNoCase("abc", "ABD"), 0);
	EXPECT_GT(ComputeLanguageCompareNoCase("abcd", "ABC"), 0);
	const LanguageNameLess less;
	EXPECT_TRUE(less(std::string("a"), std::string("B")));
	EXPECT_FALSE(less(std::string("b"), "A"));
	EXPECT_TRUE(less("A", std::string("b")));
}

TEST(LanguageMacros, ReadRows)
{
	LanguageMacros macros;
	const char first[] = "name,language,male,female,neutral,object\nadj_cs,cs,y,a,e,e\nshort,cs,m\n,cs,x,x,x,x\n";
	ComputeReadLanguageMacros(macros, first, strlen(first));
	ASSERT_EQ(macros.size(), 2u);
	EXPECT_EQ(macros["ADJ_CS"], (LanguageGenderStrings{ "y", "a", "e", "e" }));
	// [Doruk-Aksoy] Upstream read the missing cells of a short row past its end.
	EXPECT_EQ(macros["short"], (LanguageGenderStrings{ "m", "", "", "" }));

	const char second[] = "name,language,male,female,neutral,object\nadj_cs,cs,1,2,3,4\n";
	ComputeReadLanguageMacros(macros, second, strlen(second));
	EXPECT_EQ(macros["adj_cs"], (LanguageGenderStrings{ "1", "2", "3", "4" }));
	EXPECT_EQ(macros.size(), 2u);
}

TEST(LanguageMacros, Substitution)
{
	LanguageMacros macros;
	const char lump[] = "name\nadj,cs,y,a,e,o\nloop,cs,@[adj],@[adj],@[adj],@[adj]\n";
	ComputeReadLanguageMacros(macros, lump, strlen(lump));

	const LanguageMacroResult plain = ComputeSubstituteLanguageMacros("no macros", macros);
	EXPECT_EQ(plain.strings, (LanguageGenderStrings{ "no macros" }));
	EXPECT_FALSE(plain.badMacro);

	const LanguageMacroResult two = ComputeSubstituteLanguageMacros("Zabit@[ADJ] x@[adj]!", macros);
	EXPECT_EQ(two.strings, (LanguageGenderStrings{ "Zabity xy!", "Zabita xa!", "Zabite xe!", "Zabito xo!" }));

	EXPECT_EQ(ComputeSubstituteLanguageMacros("a@[nope]b", macros).strings, (LanguageGenderStrings{ "ab", "ab", "ab", "ab" }));
	EXPECT_EQ(ComputeSubstituteLanguageMacros("@[loop]", macros).strings[0], "@[adj]");
}

TEST(LanguageMacros, UnclosedMacroKeepsTheRest)
{
	LanguageMacros macros;
	const LanguageMacroResult bad = ComputeSubstituteLanguageMacros("a@[b]c@[d", macros);
	EXPECT_TRUE(bad.badMacro);
	EXPECT_EQ(bad.strings, (LanguageGenderStrings{ "ac@[d", "ac@[d", "ac@[d", "ac@[d" }));
}

TEST(LanguageMissing, Report)
{
	const LanguageMissingReport plain = ComputeLanguageMissingReport("SOME_LABEL", 64);
	EXPECT_EQ(plain.key, "SOME_LABEL");
	EXPECT_EQ(plain.message, "Translation not found 'SOME_LABEL'");

	const LanguageMissingReport controls = ComputeLanguageMissingReport("\a\b\t\n\f\rx", 64);
	EXPECT_EQ(controls.message, "Translation not found '\\a\\b\\t\\n\\f\\rx'");

	const LanguageMissingReport cut = ComputeLanguageMissingReport("abcdef", 3);
	EXPECT_EQ(cut.key, "abc...");
	EXPECT_EQ(cut.message, "Translation not found (truncated) 'abc...'");
}
