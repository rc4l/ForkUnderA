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
	"  \"schema\": 1,\n"
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
	"  \"schema\": 1,\n"
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
		"  \"schema\": 1,\n"
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
		"{ \"schema\": 1, \"id\": \"something-else\", \"name\": \"X\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_EQ("my-own-name", e.id);
}

TEST(AddonFile, AnUnknownKeyIsIgnoredRatherThanFatal)
{
	// Forward compatibility within a schema: adding an optional field must not orphan every entry
	// written before it.
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"name\": \"X\", \"nested\": { \"a\": [1, 2, {\"b\": \"}\"}] },"
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_EQ("X", e.name);
}

TEST(AddonFile, AnUnknownKeyInsideAFileIsIgnoredToo)
{
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"name\": \"X\","
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
	EXPECT_FALSE(Parse("{ \"schema\": 1, \"name\": \"X\", \"files\": [] }").valid);
}

TEST(AddonFile, AnEntryWithNoNameHasNothingToShow)
{
	EXPECT_FALSE(Parse(
		"{ \"schema\": 1,"
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
		"{ \"schema\": 1, \"name\": \"X\","
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
		std::string json = std::string("{ \"schema\": 1, \"name\": \"X\", \"map\": \"") + bad[i] +
			"\"," +
			"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }";

		EXPECT_FALSE(Parse(json.c_str()).valid) << "accepted " << bad[i];
	}
}

TEST(AddonFile, TheIwadIsOptional)
{
	// An entry that runs on whatever is already loaded need not name one.
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"name\": \"X\","
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
		std::string json = std::string("{ \"schema\": 1, \"name\": \"X\", \"files\": [{ \"name\": \"") +
			bad[i] + "\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }";

		EXPECT_FALSE(Parse(json.c_str()).valid) << "accepted " << bad[i];
	}
}

TEST(AddonFile, APathInTheIwadIsRefused)
{
	EXPECT_FALSE(Parse(
		"{ \"schema\": 1, \"name\": \"X\", \"iwad\": \"../doom2.wad\","
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
		std::string json = std::string("{ \"schema\": 1, \"name\": \"X\", \"files\": [{ \"name\": \"a.pk3\","
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
		"{ \"schema\": 1, }",
		"{ \"name\" \"X\" }",					// no colon
		"{ \"name\": \"unterminated }",
		"{ \"schema\": 1, \"files\": [ { ] }",
		"{ \"schema\": 1, \"files\": [ {} ",
		"{ \"schema\": \"one\" }",				// string where an int belongs
		"{ \"schema\": 1, \"files\": \"nope\" }",
		"{ \"schema\": 1, \"name\": \"X\" } trailing",
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
		"{ \"schema\": 1, \"name\": \"A \\\"quoted\\\" name\\twith\\\\escapes\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_EQ("A \"quoted\" name\twith\\escapes", e.name);
}

TEST(AddonFile, AnUnsupportedEscapeIsRefusedRatherThanDropped)
{
	// \u would mean committing to surrogate pairs, and nothing in this schema needs it. Refusing is
	// honest; silently dropping it would corrupt a filename.
	EXPECT_FALSE(Parse(
		"{ \"schema\": 1, \"name\": \"\\u0041\","
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
		"{ \"schema\": 1, \"summary\": \"a\\/b\\bc\\fd\\ne\\rf\","
		"  \"name\": \"X\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_EQ("a/b\bc\fd\ne\rf", e.summary);
}

TEST(AddonFile, AStringCutOffMidEscapeIsRefused)
{
	// The backslash promises another character and the file ends instead.
	EXPECT_FALSE(Parse("{ \"schema\": 1, \"name\": \"X\\").valid);
}

TEST(AddonFile, ASignedSchemaIsStillANumber)
{
	// JSON does not allow a leading +, but the reader accepts one rather than mis-reading the rest of
	// the file, and 1 is 1 however it was written.
	EXPECT_TRUE(Parse(
		"{ \"schema\": +1, \"name\": \"X\","
		"  \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }").valid);
}

TEST(AddonFile, AnEmptyFileListIsReadAndThenRefusedForBeingEmpty)
{
	// It PARSES: [] is well formed. It is refused a step later, for loading nothing, and the
	// distinction is what keeps the reason accurate.
	const AddonEntry e = Parse("{ \"schema\": 1, \"name\": \"X\", \"files\": [] }");

	EXPECT_FALSE(e.valid);
	EXPECT_EQ("no files", e.error);
}

TEST(AddonFile, AFileEntryWithNoKeysHasNoNameToLoad)
{
	const AddonEntry e = Parse("{ \"schema\": 1, \"name\": \"X\", \"files\": [{}] }");

	EXPECT_FALSE(e.valid);
	EXPECT_FALSE(e.error.empty());
}

TEST(AddonFile, ADotDotWithNoSlashIsStillRefused)
{
	// [rc4l] The existing path cases all carry a separator, so the separator check refused them first
	// and this one never ran. A name is refused for containing "..", separator or not, because the
	// resolver it feeds is not the only thing that will ever read these.
	EXPECT_FALSE(Parse(
		"{ \"schema\": 1, \"name\": \"X\","
		"  \"files\": [{ \"name\": \"a..pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }").valid);
}

TEST(AddonFile, AnEmptyFileNameIsRefused)
{
	// Not a path, but not a filename either, and the loader would search for nothing.
	EXPECT_FALSE(Parse(
		"{ \"schema\": 1, \"name\": \"X\","
		"  \"files\": [{ \"name\": \"\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }").valid);
}

TEST(AddonFile, AnUnknownKeyWithNothingAfterItIsRefused)
{
	// The skip has to notice it ran out rather than report success on an empty remainder.
	EXPECT_FALSE(Parse("{ \"schema\": 1, \"x\":").valid);
}

TEST(AddonFile, AnUnterminatedStringInsideASkippedValueIsRefused)
{
	// Inside a container we are skipping wholesale, a string still has to close: without that the
	// scan would run past a brace hidden in the text and re-sync somewhere meaningless.
	EXPECT_FALSE(Parse("{ \"schema\": 1, \"x\": { \"a\": \"never closed } }").valid);
}

TEST(AddonFile, AFileFieldOfTheWrongShapeIsRefused)
{
	const char *bad[] = {
		"{ \"schema\": 1, \"name\": \"X\", \"files\": [{ \"name\": 5 }] }",
		"{ \"schema\": 1, \"name\": \"X\", \"files\": [{ \"md5\": 5 }] }",
		"{ \"schema\": 1, \"name\": \"X\", \"files\": [{ \"notes\": }] }",
		// [rc4l] An unknown key inside a file whose value cannot be skipped at all: the string never
		// closes, so the skip fails rather than quietly swallowing the rest of the file.
		"{ \"schema\": 1, \"name\": \"X\", \"files\": [{ \"notes\": \"unterminated }] }",
		"{ \"schema\": 1, \"name\": \"X\", \"files\": [{ \"notes\": [1, 2 }] }",

		// A size that is not a number, and one that is negative. Refused rather than clamped, so a
		// typo stays visible instead of becoming a plausible zero.
		"{ \"schema\": 1, \"name\": \"X\", \"files\": [{ \"size\": \"big\" }] }",
		"{ \"schema\": 1, \"name\": \"X\", \"files\": [{ \"size\": -1 }] }",
		"{ \"schema\": 1, \"name\": \"X\", \"files\": [{ \"name\" \"a.pk3\" }] }",
		"{ \"schema\": 1, \"name\": \"X\", \"files\": [{ \"name\": \"a.pk3\" \"md5\": \"x\" }] }",
		"{ \"schema\": 1, \"name\": \"X\", \"files\": [ 5 ] }",
	};

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		const AddonEntry e = Parse(bad[i]);
		EXPECT_FALSE(e.valid) << "accepted: " << bad[i];
		EXPECT_FALSE(e.error.empty()) << "no reason given for: " << bad[i];
	}
}

// ---------------------------------------------------------------- file sizes

TEST(AddonFile, ReadsTheSizeOfEachFile)
{
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"name\": \"X\", \"files\": ["
		"  { \"name\": \"big.pk3\",   \"md5\": \"aa3896cb47c781facab7ea7f39395201\", \"size\": 240857647 },"
		"  { \"name\": \"small.pk3\", \"md5\": \"25b2a3c4f46e50f4016b640119aefae6\", \"size\": 1012005 }"
		"] }");

	ASSERT_TRUE(e.valid) << e.error;
	ASSERT_EQ(2u, e.files.size());
	EXPECT_EQ(240857647ull, e.files[0].size);
	EXPECT_EQ(1012005ull, e.files[1].size);
}

TEST(AddonFile, AnEntryWithoutSizesStillLoads)
{
	// [rc4l] Optional on purpose. Every entry written before sizes existed must keep working, and
	// say nothing about size rather than claiming zero bytes.
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"name\": \"X\", \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\" }] }");

	ASSERT_TRUE(e.valid) << e.error;
	ASSERT_EQ(1u, e.files.size());
	EXPECT_EQ(0ull, e.files[0].size) << "0 means unknown, and the panel draws nothing for it";
}

TEST(AddonFile, ZeroIsAcceptedAndMeansTheSameAsAbsent)
{
	const AddonEntry e = Parse(
		"{ \"schema\": 1, \"name\": \"X\", \"files\": [{ \"name\": \"a.pk3\", \"md5\": \"aa3896cb47c781facab7ea7f39395201\", \"size\": 0 }] }");

	ASSERT_TRUE(e.valid) << e.error;
	EXPECT_EQ(0ull, e.files[0].size);
}
