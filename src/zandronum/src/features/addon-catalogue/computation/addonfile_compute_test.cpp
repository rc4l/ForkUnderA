// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/addon-catalogue/computation/addonfile_compute.h"

#include <string>

using zx::AddonEntry;
using zx::kAddonSchema;
using zx::ParseAddonFile;

namespace
{

const char *kDuel40 =
	"{\n"
	"  \"schema\": 1, \"kind\": \"pvp\",\n"
	"  \"name\": \"Duel 40\",\n"
	"  \"summary\": \"Forty-map duel pack with a spree announcer.\",\n"
	"  \"iwad\": \"doom2.wad\",\n"
	"  \"files\": [\n"
	"    { \"name\": \"duel40b.pk3\",         \"md5\": \"aa3896cb47c781facab7ea7f39395201\" },\n"
	"    { \"name\": \"zandrospree2rc2.pk3\", \"md5\": \"25b2a3c4f46e50f4016b640119aefae6\" }\n"
	"  ]\n"
	"}\n";

const char *kDuel40Mapped =
	"{\n"
	"  \"schema\": 1, \"kind\": \"pvp\",\n"
	"  \"name\": \"Duel 40\",\n"
	"  \"iwad\": \"doom2.wad\",\n"
	"  \"map\": \"START\",\n"
	"  \"files\": [\n"
	"    { \"name\": \"duel40b.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }\n"
	"  ]\n"
	"}\n";

AddonEntry Parse(const char *json)
{
	return ParseAddonFile("duel40", json);
}

// A pack with several ways to play it, written the way Skulltag would be: one file list, one cfg per
// variant, and the default keeping server.cfg so an older build lands on the same thing.
std::string WithVariants(const char *variants)
{
	std::string json =
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"Skulltag\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"41630bc75af4b51fe5d163fe4d434c6e\" }],"
		"  \"variants\": ";
	json += variants;
	json += " }";
	return json;
}

// A pack whose ways of playing share NOTHING, written the way Ghouls vs Humans is: no entry-level
// files at all, and every variant bringing its own.
std::string NoBase(const char *variants)
{
	std::string json = "{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"Ghouls vs Humans\", \"variants\": ";
	json += variants;
	json += " }";
	return json;
}

} // namespace

// ---------------------------------------------------------------- the real file

TEST(AddonFile, ReadsTheRealDuel40Entry)
{
	// Byte-for-byte what ships in catalogue/duel40/addon.json.
	const AddonEntry e = Parse(kDuel40);

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_EQ("duel40", e.id);
	EXPECT_EQ("Duel 40", e.name);
	EXPECT_EQ("doom2.wad", e.iwad);

	ASSERT_EQ(2u, e.files.size());
	EXPECT_EQ("duel40b.pk3", e.files[0].name);
	EXPECT_EQ("aa3896cb47c781facab7ea7f39395201", e.files[0].md5);
	EXPECT_EQ("zandrospree2rc2.pk3", e.files[1].name);
	EXPECT_EQ("25b2a3c4f46e50f4016b640119aefae6", e.files[1].md5);
}

TEST(AddonFile, ReadsTheRealSkulltagEntry)
{
	// The second shipped entry, and the one that proves an entry is N files rather than one: the
	// content pk3 plus the announcer, in load order. skulltag_actors.pk3 is absent on purpose,
	// because every ZandroX release already carries it.
	const AddonEntry e = ParseAddonFile("skulltag",
		"{\n"
		"  \"schema\": 1, \"kind\": \"pvp\",\n"
		"  \"name\": \"Skulltag\",\n"
		"  \"summary\": \"Skulltag's maps, weapons and runes, with a spree announcer.\",\n"
		"  \"iwad\": \"doom2.wad\",\n"
		"  \"files\": [\n"
		"    { \"name\": \"skulltag_content-3.2-beta2.pk3\", \"md5\": \"41630bc75af4b51fe5d163fe4d434c6e\" },\n"
		"    { \"name\": \"zandrospree2rc2.pk3\",            \"md5\": \"25b2a3c4f46e50f4016b640119aefae6\" }\n"
		"  ]\n"
		"}\n");

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_EQ("skulltag", e.id);
	EXPECT_EQ("Skulltag", e.name);

	ASSERT_EQ(2u, e.files.size());
	EXPECT_EQ("skulltag_content-3.2-beta2.pk3", e.files[0].name);
	EXPECT_EQ("41630bc75af4b51fe5d163fe4d434c6e", e.files[0].md5);
	EXPECT_EQ("zandrospree2rc2.pk3", e.files[1].name);
}

TEST(AddonFile, LoadOrderIsTheOrderListed)
{
	// The announcer must land after the maps, and the only thing saying so is the order in the file.
	const AddonEntry e = Parse(kDuel40);

	ASSERT_EQ(2u, e.files.size());
	EXPECT_EQ("duel40b.pk3", e.files[0].name);
}

TEST(AddonFile, TheIdComesFromTheCallerNotTheFile)
{
	// The folder is what a player renames, so it is the identity. A file claiming otherwise is
	// ignored rather than believed.
	const AddonEntry e = ParseAddonFile("my-own-name",
		"{ \"schema\": 1, \"kind\": \"pvp\", \"id\": \"something-else\", \"name\": \"X\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_EQ("my-own-name", e.id);
}

TEST(AddonFile, AnUnknownKeyIsIgnoredRatherThanFatal)
{
	// Forward compatibility within a schema: adding an optional field must not orphan every entry
	// written before it.
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\", \"nested\": { \"a\": [1, 2, {\"b\": \"}\"}] },"
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_EQ("X", e.name);
}

TEST(AddonFile, AnUnknownKeyInsideAFileIsIgnoredToo)
{
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"size\": 123,"
		"                \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	ASSERT_TRUE(e.valid) << e.error;
	ASSERT_EQ(1u, e.files.size());
	EXPECT_EQ("a.pk3", e.files[0].name);
}

// ---------------------------------------------------------------- schema

TEST(AddonFile, AnEntryFromTheFutureIsSkippedNotGuessedAt)
{
	// User entries outlive the build that read them. A field whose meaning changed is worse than an
	// entry that does not appear.
	char json[256];
	snprintf(json, sizeof(json),
		"{ \"schema\": %d, \"name\": \"X\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }",
		kAddonSchema + 1);

	const AddonEntry e = Parse(json);
	EXPECT_FALSE(e.valid);
	EXPECT_NE(std::string::npos, e.error.find("newer"));
}

TEST(AddonFile, NoSchemaIsRefused)
{
	const AddonEntry e = Parse(
		"{ \"name\": \"X\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	EXPECT_FALSE(e.valid);
}

TEST(AddonFile, ANonsenseSchemaIsRefused)
{
	const AddonEntry e = Parse(
		"{ \"schema\": 0, \"name\": \"X\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	EXPECT_FALSE(e.valid);
}

// ---------------------------------------------------------------- what must be there

TEST(AddonFile, AnEntryWithNoFilesLoadsNothingAndIsRefused)
{
	EXPECT_FALSE(Parse("{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\", \"files\": [] }").valid);
}

TEST(AddonFile, AnEntryWithNoNameHasNothingToShow)
{
	EXPECT_FALSE(Parse(
		"{ \"schema\": 1, \"kind\": \"pvp\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }").valid);
}

TEST(AddonFile, AnEmptyObjectIsRefused)
{
	EXPECT_FALSE(Parse("{}").valid);
}

TEST(AddonFile, AnEntryMayNameTheMapItOpensOn)
{
	// Duel 40 opens on START, a welcome map it deliberately leaves OUT of its rotation, so this
	// cannot be derived from server.cfg without landing players on a duel map they never picked.
	const AddonEntry e = Parse(kDuel40Mapped);

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_EQ("START", e.map);
}

TEST(AddonFile, TheMapIsOptional)
{
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_TRUE(e.map.empty()) << "no map means the cfg rotation decides";
}

TEST(AddonFile, AMapThatIsNotAPlainLumpNameIsRefused)
{
	// It reaches a command line, so it gets the same treatment as a WAD name: no path, and nothing
	// that reads as another flag.
	const char *bad[] = { "../secret", "maps/START", "-host", "+map", "C:START" };

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		std::string json = std::string("{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\", \"map\": \"") + bad[i] +
			"\"," +
			"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }";

		EXPECT_FALSE(Parse(json.c_str()).valid) << "accepted " << bad[i];
	}
}

TEST(AddonFile, TheIwadIsOptional)
{
	// An entry that runs on whatever is already loaded need not name one.
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_TRUE(e.iwad.empty());
}

// ---------------------------------------------------------------- refusing what should not load

TEST(AddonFile, APathInAFileNameIsRefused)
{
	// The loader resolves from its own search path. A path here reaches somewhere it was not meant
	// to, and the host command line already refuses the same shape.
	const char *bad[] = {
		"../../windows/system32/x.pk3",
		"sub/dir/a.pk3",
		"sub\\dir\\a.pk3",
		"C:a.pk3",
	};

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		std::string json = std::string("{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\", \"files\": [{ \"name\": \"") +
			bad[i] + "\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }";

		EXPECT_FALSE(Parse(json.c_str()).valid) << "accepted " << bad[i];
	}
}

TEST(AddonFile, APathInTheIwadIsRefused)
{
	EXPECT_FALSE(Parse(
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\", \"iwad\": \"../doom2.wad\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }").valid);
}

TEST(AddonFile, AnUnusableMd5IsRefused)
{
	// The digest is how the by-hash store finds the file, so a wrong-looking one is a download that
	// can never resolve. Upper case is refused too, since the store keys on one spelling.
	const char *bad[] = {
		"",
		"aa3896cb47c781facab7ea7f3939520",		// 31
		"aa3896cb47c781facab7ea7f393952011",	// 33
		"AA3896CB47C781FACAB7EA7F39395201",		// upper
		"zz3896cb47c781facab7ea7f39395201",		// not hex
	};

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		std::string json = std::string("{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\", \"files\": [{ \"name\": \"a.pk3\","
			" \"md5\": \"") + bad[i] + "\" }] }";

		EXPECT_FALSE(Parse(json.c_str()).valid) << "accepted '" << bad[i] << "'";
	}
}

// ---------------------------------------------------------------- malformed input

TEST(AddonFile, RubbishIsRefusedRatherThanRead)
{
	const char *bad[] = {
		"",
		"   ",
		"[]",
		"not json at all",
		"{",
		"{ \"schema\": 1",
		"{ \"schema\": 1, \"kind\": \"pvp\", }",
		"{ \"name\" \"X\" }",					// no colon
		"{ \"name\": \"unterminated }",
		"{ \"schema\": 1, \"kind\": \"pvp\", \"files\": [ { ] }",
		"{ \"schema\": 1, \"kind\": \"pvp\", \"files\": [ {} ",
		"{ \"schema\": \"one\" }",				// string where an int belongs
		"{ \"schema\": 1, \"kind\": \"pvp\", \"files\": \"nope\" }",
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\" } trailing",
	};

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		const AddonEntry e = Parse(bad[i]);
		EXPECT_FALSE(e.valid) << "accepted: " << bad[i];
		EXPECT_FALSE(e.error.empty()) << "no reason given for: " << bad[i];
	}
}

TEST(AddonFile, EveryTruncationOfARealEntryIsRefused)
{
	// The file can be cut short by a failed download or a full disk, and a half-read entry that looks
	// valid is worse than one that does not appear.
	//
	// Measured against the file with trailing whitespace removed, because losing the final newline
	// is not losing anything: what is left is still a complete object, and demanding otherwise would
	// be testing the test rather than the parser.
	std::string meaningful = kDuel40;
	while (!meaningful.empty() && std::isspace(static_cast<unsigned char>(meaningful[meaningful.size() - 1])))
		meaningful.erase(meaningful.size() - 1);

	ASSERT_TRUE(Parse(meaningful.c_str()).valid) << "the whole thing should still parse";

	for (size_t n = 0; n < meaningful.size(); ++n)
		EXPECT_FALSE(Parse(meaningful.substr(0, n).c_str()).valid) << "accepted a " << n << " byte file";
}

TEST(AddonFile, EscapesInStringsAreUnderstood)
{
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"A \\\"quoted\\\" name\\twith\\\\escapes\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_EQ("A \"quoted\" name\twith\\escapes", e.name);
}

TEST(AddonFile, AnUnsupportedEscapeIsRefusedRatherThanDropped)
{
	// \u would mean committing to surrogate pairs, and nothing in this schema needs it. Refusing is
	// honest; silently dropping it would corrupt a filename.
	EXPECT_FALSE(Parse(
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"\\u0041\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }").valid);
}

TEST(AddonFile, ANegativeSchemaIsRefused)
{
	EXPECT_FALSE(Parse("{ \"schema\": -1, \"name\": \"X\" }").valid);
}

TEST(AddonFile, EveryEscapeTheSchemaAcceptsIsUnderstood)
{
	// [rc4l] All of them, not a sample. Each is its own line in the reader, and an untested one is a
	// filename we would silently mangle the first time somebody used it.
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"kind\": \"pvp\", \"summary\": \"a\\/b\\bc\\fd\\ne\\rf\","
		"  \"name\": \"X\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_EQ("a/b\bc\fd\ne\rf", e.summary);
}

TEST(AddonFile, AStringCutOffMidEscapeIsRefused)
{
	// The backslash promises another character and the file ends instead.
	EXPECT_FALSE(Parse("{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\\").valid);
}

TEST(AddonFile, ASignedSchemaIsStillANumber)
{
	// JSON does not allow a leading +, but the reader accepts one rather than mis-reading the rest of
	// the file, and 1 is 1 however it was written.
	EXPECT_TRUE(Parse(
		"{ \"schema\": +1, \"kind\": \"pvp\", \"name\": \"X\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }").valid);
}

TEST(AddonFile, AnEmptyFileListIsReadAndThenRefusedForBeingEmpty)
{
	// It PARSES: [] is well formed. It is refused a step later, for loading nothing, and the
	// distinction is what keeps the reason accurate.
	const AddonEntry e = Parse("{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\", \"files\": [] }");

	EXPECT_FALSE(e.valid);
	EXPECT_EQ("no files", e.error);
}

TEST(AddonFile, AFileEntryWithNoKeysHasNoNameToLoad)
{
	const AddonEntry e = Parse("{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\", \"files\": [{}] }");

	EXPECT_FALSE(e.valid);
	EXPECT_FALSE(e.error.empty());
}

TEST(AddonFile, ADotDotWithNoSlashIsStillRefused)
{
	// [rc4l] The existing path cases all carry a separator, so the separator check refused them first
	// and this one never ran. A name is refused for containing "..", separator or not, because the
	// resolver it feeds is not the only thing that will ever read these.
	EXPECT_FALSE(Parse(
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\","
		"  \"files\": [{ \"name\": \"a..pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }").valid);
}

TEST(AddonFile, AnEmptyFileNameIsRefused)
{
	// Not a path, but not a filename either, and the loader would search for nothing.
	EXPECT_FALSE(Parse(
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\","
		"  \"files\": [{ \"name\": \"\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }").valid);
}

TEST(AddonFile, AnUnknownKeyWithNothingAfterItIsRefused)
{
	// The skip has to notice it ran out rather than report success on an empty remainder.
	EXPECT_FALSE(Parse("{ \"schema\": 1, \"kind\": \"pvp\", \"x\":").valid);
}

TEST(AddonFile, AnUnterminatedStringInsideASkippedValueIsRefused)
{
	// Inside a container we are skipping wholesale, a string still has to close: without that the
	// scan would run past a brace hidden in the text and re-sync somewhere meaningless.
	EXPECT_FALSE(Parse("{ \"schema\": 1, \"kind\": \"pvp\", \"x\": { \"a\": \"never closed } }").valid);
}

TEST(AddonFile, AFileFieldOfTheWrongShapeIsRefused)
{
	const char *bad[] = {
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\", \"files\": [{ \"name\": 5 }] }",
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\", \"files\": [{ \"md5\": 5 }] }",
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\", \"files\": [{ \"notes\": }] }",
		// [rc4l] An unknown key inside a file whose value cannot be skipped at all: the string never
		// closes, so the skip fails rather than quietly swallowing the rest of the file.
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\", \"files\": [{ \"notes\": \"unterminated }] }",
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\", \"files\": [{ \"notes\": [1, 2 }] }",

		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\", \"files\": [{ \"name\" \"a.pk3\" }] }",
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\", \"files\": [{ \"name\": \"a.pk3\" \"md5\": \"x\" }] }",
		"{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\", \"files\": [ 5 ] }",
	};

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		const AddonEntry e = Parse(bad[i]);
		EXPECT_FALSE(e.valid) << "accepted: " << bad[i];
		EXPECT_FALSE(e.error.empty()) << "no reason given for: " << bad[i];
	}
}

// ------------------------------------------------------------------ variants

TEST(AddonFile, AnEntryWithNoVariantsHasNone)
{
	// The ordinary case, and the one that must not change: "plays one way" is not "has one variant",
	// and the panel draws nothing rather than a row of one.
	const AddonEntry e = Parse(kDuel40);

	EXPECT_TRUE(e.valid);
	EXPECT_TRUE(e.variants.empty());
}

TEST(AddonFile, VariantsAreReadInOrderWithTheirTooltips)
{
	const AddonEntry e = Parse(WithVariants(
		"[ { \"id\": \"dm\", \"name\": \"Deathmatch\", \"cfg\": \"server.cfg\", \"kind\": \"pvp\", \"default\": true },"
		"  { \"id\": \"inv\", \"name\": \"Invasion\", \"cfg\": \"invasion.cfg\", \"kind\": \"pve\","
		"    \"tooltip\": \"Waves of monsters. Survive together.\" } ]").c_str());

	ASSERT_TRUE(e.valid) << e.error;
	ASSERT_EQ(2u, e.variants.size());

	EXPECT_EQ("dm", e.variants[0].id);
	EXPECT_EQ("Deathmatch", e.variants[0].name);
	EXPECT_EQ("server.cfg", e.variants[0].cfg);
	EXPECT_TRUE(e.variants[0].isDefault);
	EXPECT_TRUE(e.variants[0].tooltip.empty());

	EXPECT_EQ(zx::VariantKind::PvP, e.variants[0].kind);

	EXPECT_EQ("inv", e.variants[1].id);
	EXPECT_EQ("Waves of monsters. Survive together.", e.variants[1].tooltip);
	EXPECT_EQ(zx::VariantKind::PvE, e.variants[1].kind);
	EXPECT_FALSE(e.variants[1].isDefault);
}

TEST(AddonFile, AVariantListMayClaimNoDefaultAtAll)
{
	const AddonEntry e = Parse(WithVariants(
		"[ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\" },"
		"  { \"id\": \"b\", \"name\": \"B\", \"cfg\": \"b.cfg\", \"kind\": \"pvp\" } ]").c_str());

	EXPECT_TRUE(e.valid) << e.error;
}

TEST(AddonFile, TwoDefaultsIsAMistakeRatherThanAnOrdering)
{
	// Which way a pack plays when nobody has chosen must not depend on how the file is sorted, so an
	// ambiguous claim is refused where the author can see it.
	const AddonEntry e = Parse(WithVariants(
		"[ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\", \"default\": true },"
		"  { \"id\": \"b\", \"name\": \"B\", \"cfg\": \"b.cfg\", \"kind\": \"pvp\", \"default\": true } ]").c_str());

	EXPECT_FALSE(e.valid);
	EXPECT_FALSE(e.error.empty());
}

TEST(AddonFile, TwoVariantsMayNotShareAnId)
{
	// The id is what a remembered choice is keyed on, so duplicates make the choice meaningless.
	const AddonEntry e = Parse(WithVariants(
		"[ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\" },"
		"  { \"id\": \"a\", \"name\": \"B\", \"cfg\": \"b.cfg\", \"kind\": \"pvp\" } ]").c_str());

	EXPECT_FALSE(e.valid);
}

TEST(AddonFile, AVariantOfTheWrongShapeIsRefused)
{
	const char *bad[] = {
		// No id, so nothing to remember a choice by.
		"[ { \"name\": \"A\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\" } ]",
		// No name, so nothing to show.
		"[ { \"id\": \"a\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\" } ]",
		// No cfg at all, which would silently fall back to the pack's own and play the wrong thing.
		"[ { \"id\": \"a\", \"name\": \"A\" } ]",
		// A cfg that climbs out of the entry's folder, the same rule every filename here obeys.
		"[ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"../server.cfg\" } ]",
		"[ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"sub/a.cfg\" } ]",
		// "default" is a boolean and only a boolean: a guess here silently changes how a pack plays.
		"[ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\", \"default\": 1 } ]",
		"[ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\", \"default\": \"yes\" } ]",
		// Malformed array shapes.
		"[ 5 ]",
		"[ { \"id\": \"a\" \"name\": \"A\" } ]",
		"[ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\" }",
	};

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		const AddonEntry e = Parse(WithVariants(bad[i]).c_str());
		EXPECT_FALSE(e.valid) << "accepted: " << bad[i];
		EXPECT_FALSE(e.error.empty()) << "no reason given for: " << bad[i];
	}
}

// ------------------------------------------------------------ the label

TEST(AddonFile, AnExperienceMustSayWhetherItIsPveOrPvp)
{
	// The first thing anybody wants to know, and the one thing the name reliably fails to say.
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"name\": \"X\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	EXPECT_FALSE(e.valid);
	EXPECT_NE(std::string::npos, e.error.find("pve or pvp"));
	EXPECT_NE(std::string::npos, e.error.find("experience"));
}

TEST(AddonFile, AnUnlabelledVariantIsRefusedByItsOwnName)
{
	// A pack can have six of them, so "a variant" would leave the author reading all six to find the
	// one they forgot.
	const AddonEntry e = Parse(WithVariants(
		"[ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\" },"
		"  { \"id\": \"b\", \"name\": \"Invasion\", \"cfg\": \"b.cfg\" } ]").c_str());

	EXPECT_FALSE(e.valid);
	EXPECT_NE(std::string::npos, e.error.find("Invasion"));
	EXPECT_NE(std::string::npos, e.error.find("pve or pvp"));
	EXPECT_NE(std::string::npos, e.error.find("experience variant"));
}

TEST(AddonFile, AKindItDoesNotRecogniseIsRefusedRatherThanGuessedAt)
{
	const char *bad[] = { "coop", "PVP", "pvpve", "", "1" };

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		std::string json =
			"{ \"schema\": 1, \"name\": \"X\", \"kind\": \"";
		json += bad[i];
		json += "\", \"files\": [{ \"name\": \"a.pk3\","
			"  \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }";

		EXPECT_FALSE(Parse(json.c_str()).valid) << "accepted: " << bad[i];
	}
}

TEST(AddonFile, AnEntryWithVariantsIsLabelledByThemRatherThanByItself)
{
	// The variants ARE the experiences there, so the entry does not need its own label. Requiring one
	// as well would mean writing "pvp" on a pack that is half invasion.
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"name\": \"Skulltag\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"41630bc75af4b51fe5d163fe4d434c6e\" }],"
		"  \"variants\": [ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\" },"
		"                  { \"id\": \"b\", \"name\": \"B\", \"cfg\": \"b.cfg\", \"kind\": \"pve\" } ] }");

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_EQ(zx::VariantKind::PvP, e.variants[0].kind);
	EXPECT_EQ(zx::VariantKind::PvE, e.variants[1].kind);
}

TEST(AddonFile, EveryKindHasAWordForIt)
{
	// Used on the panel and in messages, so none of them may come out empty.
	EXPECT_STREQ("PvP", zx::DescribeVariantKind(zx::VariantKind::PvP));
	EXPECT_STREQ("PvE", zx::DescribeVariantKind(zx::VariantKind::PvE));
	EXPECT_STREQ("Unlabelled", zx::DescribeVariantKind(zx::VariantKind::Unknown));
	EXPECT_STREQ("Unlabelled", zx::DescribeVariantKind(static_cast<zx::VariantKind>(99)));
}

TEST(AddonFile, AnEmptyVariantsArrayIsSimplyNoVariants)
{
	// Not a refusal: an author trimming the list back to one way of playing should not have to also
	// remember to delete the key.
	const AddonEntry e = Parse(WithVariants("[]").c_str());

	EXPECT_TRUE(e.valid) << e.error;
	EXPECT_TRUE(e.variants.empty());
}

TEST(AddonFile, AnUnknownKeyInsideAVariantIsIgnoredRatherThanRefused)
{
	// Same reading as everywhere else in this file, so a catalogue written for a later build still
	// loads here instead of disappearing.
	const AddonEntry e = Parse(WithVariants(
		"[ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\", \"colour\": \"red\" } ]").c_str());

	ASSERT_TRUE(e.valid) << e.error;
	ASSERT_EQ(1u, e.variants.size());
	EXPECT_EQ("a", e.variants[0].id);
}

// ------------------------------------------------------------------ variants with their own files

TEST(AddonFile, AVariantMayCarryFilesOfItsOwn)
{
	// Skulltag's shape stays legal and gains nothing; this is the addition on top of it.
	const AddonEntry e = Parse(WithVariants(
		"[ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\","
		"    \"files\": [{ \"name\": \"extra.pk3\", \"md5\": \"41630bc75af4b51fe5d163fe4d434c6f\" }] } ]").c_str());

	ASSERT_TRUE(e.valid) << e.error;
	ASSERT_EQ(1u, e.variants.size());
	ASSERT_EQ(1u, e.variants[0].files.size());
	EXPECT_EQ("extra.pk3", e.variants[0].files[0].name);
	EXPECT_EQ("41630bc75af4b51fe5d163fe4d434c6f", e.variants[0].files[0].md5);

	EXPECT_EQ(1u, e.files.size()) << "the entry's own list is untouched by a variant's";
}

TEST(AddonFile, AnEntryMayKeepNoFilesAtAllWhenEveryVariantBringsItsOwn)
{
	// [rc4l] Ghouls vs Humans: the ways of playing share no base whatever, so the entry itself loads
	// nothing and each variant brings a whole different wad. Requiring files at the entry level used
	// to refuse exactly this shape.
	const AddonEntry e = Parse(NoBase(
		"[ { \"id\": \"gvh\", \"name\": \"Ghouls\", \"cfg\": \"server.cfg\", \"kind\": \"pvp\", \"default\": true,"
		"    \"files\": [{ \"name\": \"gvh.pk3\", \"md5\": \"41630bc75af4b51fe5d163fe4d434c6e\" }] },"
		"  { \"id\": \"gvhr\", \"name\": \"Reborn\", \"cfg\": \"reborn.cfg\", \"kind\": \"pvp\","
		"    \"files\": [{ \"name\": \"gvhr.pk3\", \"md5\": \"41630bc75af4b51fe5d163fe4d434c6f\" }] } ]").c_str());

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_TRUE(e.files.empty());
	ASSERT_EQ(2u, e.variants.size());
	EXPECT_EQ("gvh.pk3", e.variants[0].files[0].name);
	EXPECT_EQ("gvhr.pk3", e.variants[1].files[0].name);
}

TEST(AddonFile, AVariantThatLoadsNothingIsRefusedByItsOwnName)
{
	// With no base to inherit, a variant with no files of its own would start a server on the bare
	// IWAD. Named for the same reason the missing kind is: a pack can have six.
	const AddonEntry e = Parse(NoBase(
		"[ { \"id\": \"gvh\", \"name\": \"Ghouls\", \"cfg\": \"server.cfg\", \"kind\": \"pvp\","
		"    \"files\": [{ \"name\": \"gvh.pk3\", \"md5\": \"41630bc75af4b51fe5d163fe4d434c6e\" }] },"
		"  { \"id\": \"empty\", \"name\": \"Reborn\", \"cfg\": \"reborn.cfg\", \"kind\": \"pvp\" } ]").c_str());

	EXPECT_FALSE(e.valid);
	EXPECT_NE(std::string::npos, e.error.find("Reborn")) << "say WHICH one: " << e.error;
}

TEST(AddonFile, AnEntryWithABaseCoversAVariantThatAddsNothing)
{
	// The other side of the same rule. Skulltag's variants differ by cfg alone and load the entry's
	// files, which is not a variant that loads nothing.
	const AddonEntry e = Parse(WithVariants(
		"[ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\" } ]").c_str());

	EXPECT_TRUE(e.valid) << e.error;
}

TEST(AddonFile, AVariantsFilesObeyTheSameRulesAsTheEntrys)
{
	// They reach the same loader and the same by-hash store, so a path or a bad hash is exactly as
	// dangerous there as at the entry level.
	const char *bad[] = {
		"[ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\","
		"    \"files\": [{ \"name\": \"../gvh.pk3\", \"md5\": \"41630bc75af4b51fe5d163fe4d434c6e\" }] } ]",

		"[ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\","
		"    \"files\": [{ \"name\": \"wads/gvh.pk3\", \"md5\": \"41630bc75af4b51fe5d163fe4d434c6e\" }] } ]",

		"[ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\","
		"    \"files\": [{ \"name\": \"gvh.pk3\", \"md5\": \"nothex\" }] } ]",

		"[ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\","
		"    \"files\": [{ \"name\": \"gvh.pk3\" }] } ]",

		// The array itself, not one of its members.
		"[ { \"id\": \"a\", \"name\": \"A\", \"cfg\": \"a.cfg\", \"kind\": \"pvp\", \"files\": 5 } ]",
	};

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		const AddonEntry e = Parse(WithVariants(bad[i]).c_str());
		EXPECT_FALSE(e.valid) << "accepted: " << bad[i];
		EXPECT_FALSE(e.error.empty()) << "no reason given for: " << bad[i];
	}
}

TEST(AddonFile, AnEntryWithNeitherFilesNorVariantsIsStillRefused)
{
	// Relaxing the entry-level rule for the Ghouls shape must not have relaxed it into nothing.
	const AddonEntry e = Parse("{ \"schema\": 1, \"kind\": \"pvp\", \"name\": \"X\" }");

	EXPECT_FALSE(e.valid);
	EXPECT_FALSE(e.error.empty());
}

// ------------------------------------------------------------ the other document

namespace
{

zx::AddonRemix ParseRemix(const char *json)
{
	return zx::ParseRemixFile("survival", json);
}

} // namespace

TEST(RemixFile, TheSmallestUsableRemixIsJustANameAndACfg)
{
	// [rc4l] A rules remix: no files at all, one line of cfg. Deliberately not refused for loading
	// nothing, since the baseline option loads nothing either and still needs a name.
	const zx::AddonRemix r = ParseRemix(
		"{ \"schema\": 1, \"name\": \"Survival\", \"cfg\": \"survival.cfg\" }");

	ASSERT_TRUE(r.valid) << r.error;
	EXPECT_EQ("survival", r.id) << "the id comes from the folder, never from the file";
	EXPECT_EQ("Survival", r.name);
	EXPECT_EQ("survival.cfg", r.cfg);
	EXPECT_TRUE(r.files.empty());
}

TEST(RemixFile, ARemixWithNoGroupIsInTheDefaultOne)
{
	// The shape every remix written before groups existed has. It must keep parsing unchanged.
	const zx::AddonRemix r = ParseRemix("{ \"schema\": 1, \"name\": \"Survival\" }");

	ASSERT_TRUE(r.valid) << r.error;
	EXPECT_TRUE(r.group.empty());
}

TEST(RemixFile, TheGroupIsRead)
{
	const zx::AddonRemix r = ParseRemix(
		"{ \"schema\": 1, \"name\": \"Brutal Doom\", \"group\": \"mod\" }");

	ASSERT_TRUE(r.valid) << r.error;
	EXPECT_EQ("mod", r.group);
}

TEST(RemixFile, ARemixWithFilesCarriesThem)
{
	const zx::AddonRemix r = ParseRemix(
		"{ \"schema\": 1, \"name\": \"Brutal Doom\", \"group\": \"mod\","
		"  \"files\": [{ \"name\": \"brutal22test6.pk3\","
		"                \"md5\": \"57a61814fe96cfc20043f370eeace023\" }] }");

	ASSERT_TRUE(r.valid) << r.error;
	ASSERT_EQ(1u, r.files.size());
	EXPECT_EQ("brutal22test6.pk3", r.files[0].name);
}

TEST(RemixFile, ARemixWithNoNameIsRefused)
{
	// The picker has nothing to draw for it, so it would be an invisible row.
	const zx::AddonRemix r = ParseRemix("{ \"schema\": 1, \"cfg\": \"survival.cfg\" }");

	EXPECT_FALSE(r.valid);
	EXPECT_FALSE(r.error.empty());
}

TEST(RemixFile, APathInTheCfgOrTheFilesIsRefused)
{
	// Both reach a loader, so both obey the entry's rule: a bare filename beside the remix.json.
	const char *const bad[] = {
		"{ \"schema\": 1, \"name\": \"X\", \"cfg\": \"../../evil.cfg\" }",
		"{ \"schema\": 1, \"name\": \"X\", \"files\": [{ \"name\": \"a/b.pk3\","
		"   \"md5\": \"57a61814fe96cfc20043f370eeace023\" }] }",
		"{ \"schema\": 1, \"name\": \"X\", \"files\": [{ \"name\": \"b.pk3\", \"md5\": \"nothex\" }] }",
	};

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		const zx::AddonRemix r = ParseRemix(bad[i]);
		EXPECT_FALSE(r.valid) << "accepted: " << bad[i];
		EXPECT_FALSE(r.error.empty()) << "no reason given for: " << bad[i];
	}
}

TEST(RemixFile, ARemixFromTheFutureIsSkippedNotGuessedAt)
{
	const zx::AddonRemix r = ParseRemix("{ \"schema\": 99, \"name\": \"X\" }");

	EXPECT_FALSE(r.valid);
	EXPECT_FALSE(r.error.empty());
}

TEST(RemixFile, NoSchemaIsRefused)
{
	const zx::AddonRemix r = ParseRemix("{ \"name\": \"X\" }");

	EXPECT_FALSE(r.valid);
}

// ------------------------------------------------------------ curation

TEST(AddonFile, AnEntryThatSaysNothingAboutItsPlaceIsNeitherFirstNorMarked)
{
	// The default every existing entry relies on: order 0 keeps the folder order the catalogue has
	// always had, and no accent keeps the ordinary label colour.
	const AddonEntry e = Parse(kDuel40);

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_EQ(0, e.order);
	EXPECT_FALSE(e.accent);
}

TEST(AddonFile, OrderAndAccentAreRead)
{
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"kind\": \"pve\", \"name\": \"X\", \"order\": 20, \"accent\": true,"
		"  \"files\": [{ \"name\": \"x.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_EQ(20, e.order);
	EXPECT_TRUE(e.accent);
}

TEST(AddonFile, AccentCanBeSaidAndTurnedOff)
{
	// Pinned because writing the field is how an author REMOVES the mark, and a reader that only
	// looked for the key's presence would make that impossible to say.
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"kind\": \"pve\", \"name\": \"X\", \"accent\": false,"
		"  \"files\": [{ \"name\": \"x.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_FALSE(e.accent);
}

TEST(AddonFile, AnOrderThatIsNotANumberIsRefused)
{
	// Refused rather than treated as 0, which would silently drop a curated entry back into the
	// middle of the list with nothing to say why.
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"kind\": \"pve\", \"name\": \"X\", \"order\": \"first\","
		"  \"files\": [{ \"name\": \"x.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	EXPECT_FALSE(e.valid);
	EXPECT_FALSE(e.error.empty());
}

TEST(AddonFile, AnAccentThatIsNotABooleanIsRefused)
{
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"kind\": \"pve\", \"name\": \"X\", \"accent\": \"yes\","
		"  \"files\": [{ \"name\": \"x.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	EXPECT_FALSE(e.valid);
	EXPECT_FALSE(e.error.empty());
}

// ------------------------------------------------------------ weapon speed

TEST(AddonFile, AnEntryThatSaysNothingAboutWeaponSpeedDoesNotOfferTheControl)
{
	const AddonEntry e = Parse(kDuel40);

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_FALSE(e.fastWeapons);
}

TEST(AddonFile, FastWeaponsCanBeOfferedAndTakenBack)
{
	const AddonEntry on = Parse(
		"{ \"schema\": 1, \"kind\": \"pve\", \"name\": \"X\", \"fastweapons\": true,"
		"  \"files\": [{ \"name\": \"x.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");
	const AddonEntry off = Parse(
		"{ \"schema\": 1, \"kind\": \"pve\", \"name\": \"X\", \"fastweapons\": false,"
		"  \"files\": [{ \"name\": \"x.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	ASSERT_TRUE(on.valid) << on.error;
	ASSERT_TRUE(off.valid) << off.error;
	EXPECT_TRUE(on.fastWeapons);
	EXPECT_FALSE(off.fastWeapons);
}

TEST(AddonFile, AFastWeaponsThatIsNotABooleanIsRefused)
{
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"kind\": \"pve\", \"name\": \"X\", \"fastweapons\": 2,"
		"  \"files\": [{ \"name\": \"x.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	EXPECT_FALSE(e.valid);
	EXPECT_FALSE(e.error.empty());
}
