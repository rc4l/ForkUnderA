//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: refdump_test.cpp
//
// Description: The reference decode. The three reference recordings, read
// through the ported format layer by the shared dumper, must reproduce the
// dumps shipped beside them byte for byte.
//
//-----------------------------------------------------------------------------

//*****************************************************************************
//	THE REFERENCE DECODE
//
// The shared dumper (tests/snapdump/) is snap_dump, the tool that wrote the
// shipped dumps; these tests are that tool run against the ported format
// layer. A dump differing by one byte is a decode difference, which is what
// this step exists to catch before anything sends the format.

#include <gtest/gtest.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "snapdump/snapdump.h"
#include "features/wad-download/computation/digest_compute.h"

namespace
{

// [Kaori-Chani] One reference: the recording's own digest, so a wrong or damaged input fails as a
// wrong input rather than as a decode difference, and the digest its dump must have.
struct RefCase
{
	const char* pszFile;
	const char* pszInSha;
	const char* pszDumpSha;
};

const RefCase REF_WIDE =
{
	"ref-wide.csnap",
	"467139698e7b8112b298b9b19516568700c5ef0765da57ace83628e7de46485c",
	"cedfc46bea1d55d973ed6e6d8ffc68108e3ac006f245ce13d1a7bc71bc6f88ec",
};
const RefCase REF_ZSNAP =
{
	"ref.zsnap",
	"1263972e399888b0284d9d2316f6bd0013648ab5dd1267e4912967231669ecbe",
	"215cb3f8be052d7a15ff34fc2759f6d81fa9ca305533a2481950480c33c99d5f",
};
const RefCase REF_CSNAP =
{
	"ref.csnap",
	"f8a5fe59152ce31177510d2ba63618e6d6cf2f264f3af9aef2fe968fcdc7ff7d",
	"5d3e7f2d7ed3630106cc9a1d5f9bb66e557da69de5992c14d58f64c349e80111",
};

// [Kaori-Chani] The digest of a whole file, or an empty string where it cannot be read.
std::string refdump_Sha256(const std::string& path)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (f == NULL)
		return std::string();
	zx::Sha256State s;
	zx::ComputeSha256Init(s);
	unsigned char buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		zx::ComputeSha256Update(s, buf, n);
	fclose(f);
	uint8_t digest[32];
	zx::ComputeSha256Final(s, digest);
	return zx::ComputeDigestHex(digest, sizeof(digest));
}

// [Kaori-Chani] Writes one recording's dump through the shared dumper, which is the same call the
// snap_dump tool makes: the container is told by its magic, never by the file's name, and a dump
// taken by hand is the dump this test would have taken.
bool refdump_Write(const std::string& in, const std::string& out)
{
	FILE* f = fopen(out.c_str(), "wb");
	if (f == NULL)
		return false;
	// In tag mode, so a type this build has learned since the reference tag does not
	// change what the shipped dumps hold.
	const ESnapDumpResult res = SnapDumpWrite(f, in.c_str(), true);
	fclose(f);
	// [Kaori-Chani] A reference must read whole and report nothing, so only SNAPDUMP_OK will do:
	// a recording that decoded as far as an error is a decode difference like any other.
	return res == SNAPDUMP_OK;
}

// [Kaori-Chani] The first line at which two dumps differ, so a mismatch names itself even when
// the files are too large to read by eye.
std::string refdump_FirstDifference(const std::string& mine, const std::string& theirs)
{
	FILE* a = fopen(mine.c_str(), "rb");
	FILE* b = fopen(theirs.c_str(), "rb");
	if ((a == NULL) || (b == NULL))
	{
		if (a != NULL)
			fclose(a);
		if (b != NULL)
			fclose(b);
		return std::string("  (the shipped dump is not beside the recording, so no line is named;"
			" diff it against the file written above)");
	}
	std::string out;
	char la[4096], lb[4096];
	unsigned int line = 0;
	for (;;)
	{
		const char* ra = fgets(la, sizeof(la), a);
		const char* rb = fgets(lb, sizeof(lb), b);
		++line;
		if ((ra == NULL) && (rb == NULL))
		{
			out = "  (every line agrees, so the two differ only in line endings or a final newline)";
			break;
		}
		if ((ra == NULL) || (rb == NULL) || (strcmp(la, lb) != 0))
		{
			char msg[9000];
			snprintf(msg, sizeof(msg), "  first differs at line %u:\n    ours:    %s    shipped: %s",
				line, (ra != NULL) ? la : "(end of file)\n", (rb != NULL) ? lb : "(end of file)\n");
			out = msg;
			break;
		}
	}
	fclose(a);
	fclose(b);
	return out;
}

// [Kaori-Chani] Decodes one reference and holds its dump to the shipped digest.
void refdump_Check(const RefCase& ref, const std::string& dir)
{
	const std::string in = dir + "/" + ref.pszFile;
	ASSERT_EQ(ref.pszInSha, refdump_Sha256(in))
		<< in << " is not the reference " << ref.pszFile << ", or could not be read at all";

	const std::string out = std::string(ZX_SNAPSHOT_TESTOUT) + "/" + ref.pszFile + ".dump.txt";
	ASSERT_TRUE(refdump_Write(in, out)) << "the decode of " << in << " did not finish";

	const std::string got = refdump_Sha256(out);
	if (got == ref.pszDumpSha)
		return;
	FAIL() << ref.pszFile << " decodes differently from the shipped dump.\n"
		<< "  ours:    " << got << "\n"
		<< "  shipped: " << ref.pszDumpSha << "\n"
		<< "  written: " << out << "\n"
		<< refdump_FirstDifference(out, in + ".dump.txt");
}

// [Kaori-Chani] The two large recordings are not in the repo, so ZX_SNAP_REFDIR names where they
// are. Unset, the test says it did not run; set and wrong, it fails, because a mistyped path must
// never read as "not run".
const char* refdump_Dir()
{
	const char* psz = getenv("ZX_SNAP_REFDIR");
	return ((psz != NULL) && (psz[0] != 0)) ? psz : NULL;
}

} // namespace

// [Kaori-Chani] The wide reference ships in the repo and so runs on every build: it is the only
// one carrying wide frames and non-zero origins, where 48.16 is likeliest to go wrong.
TEST(SnapshotRefDump, TheWideReferenceDecodesByteForByte)
{
	refdump_Check(REF_WIDE, ZX_SNAPSHOT_TESTDATA);
}

TEST(SnapshotRefDump, TheZsnapReferenceDecodesByteForByte)
{
	const char* pszDir = refdump_Dir();
	if (pszDir == NULL)
		GTEST_SKIP() << "ZX_SNAP_REFDIR is unset, so this did not run. The step 1 gate needs it"
			" pointing at the folder holding ref.zsnap and ref.csnap; a skip is not a pass.";
	refdump_Check(REF_ZSNAP, pszDir);
}

TEST(SnapshotRefDump, TheCsnapReferenceDecodesByteForByte)
{
	const char* pszDir = refdump_Dir();
	if (pszDir == NULL)
		GTEST_SKIP() << "ZX_SNAP_REFDIR is unset, so this did not run. The step 1 gate needs it"
			" pointing at the folder holding ref.zsnap and ref.csnap; a skip is not a pass.";
	refdump_Check(REF_CSNAP, pszDir);
}
