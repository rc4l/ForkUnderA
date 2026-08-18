// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/identity/computation/identity_compute.h"

using zx::AccountNameFromDigest;
using zx::ClientAuthKeyPath;
using zx::ClientProofMessage;
using zx::FromHex;
using zx::IdentityRootUnder;
using zx::ServerAuthKeyPath;
using zx::ServerProofMessage;
using zx::ToHex;

namespace
{

std::vector<unsigned char> Bytes(size_t n, unsigned char first = 0)
{
	std::vector<unsigned char> out;
	for (size_t i = 0; i < n; ++i)
		out.push_back(static_cast<unsigned char>(first + i));
	return out;
}

const char *const kRoot = "C:/Users/x/AppData/Roaming/ForkUnderA";

} // namespace

// ---------------------------------------------------------------- hex

TEST(IdentityHex, RoundTrips)
{
	const std::vector<unsigned char> in = Bytes(32);
	std::vector<unsigned char> out;

	ASSERT_TRUE(FromHex(ToHex(in), out));
	EXPECT_EQ(in, out);
}

TEST(IdentityHex, IsAlwaysLowerCase)
{
	std::vector<unsigned char> in;
	in.push_back(0xAB);
	in.push_back(0xCD);

	// Paths are built from these. Windows folds case and Linux does not, so one upper-case digit
	// means one file on Windows and two on Linux.
	EXPECT_EQ("abcd", ToHex(in));
}

TEST(IdentityHex, ReadsEitherCaseBack)
{
	// We only ever write lower case, but the other end is not ours to assume about.
	std::vector<unsigned char> lower, upper;

	ASSERT_TRUE(FromHex("abcdef", lower));
	ASSERT_TRUE(FromHex("ABCDEF", upper));
	EXPECT_EQ(lower, upper);
}

TEST(IdentityHex, RefusesWhatIsNotHex)
{
	std::vector<unsigned char> out;

	EXPECT_FALSE(FromHex("abc", out));      // odd length is half a byte
	EXPECT_FALSE(FromHex("zz", out));       // not digits
	EXPECT_FALSE(FromHex("ab cd", out));    // spaces are not separators here

	// And leaves nothing behind, so a caller that ignores the result cannot act on half a value.
	EXPECT_TRUE(out.empty());
}

TEST(IdentityHex, EmptyIsEmptyBothWays)
{
	std::vector<unsigned char> out;

	EXPECT_EQ("", ToHex(std::vector<unsigned char>()));
	EXPECT_TRUE(FromHex("", out));
	EXPECT_TRUE(out.empty());
}

// ---------------------------------------------------------------- account names

TEST(AccountName, IsTheTruncatedDigest)
{
	const std::vector<unsigned char> digest = Bytes(32);

	EXPECT_EQ(ToHex(digest).substr(0, 32), AccountNameFromDigest(digest));
	EXPECT_EQ(32u, AccountNameFromDigest(digest).size());
}

TEST(AccountName, DifferentKeysGetDifferentNames)
{
	EXPECT_NE(AccountNameFromDigest(Bytes(32, 0)), AccountNameFromDigest(Bytes(32, 1)));
}

TEST(AccountName, ADigestTooShortToNameAnythingIsRefused)
{
	// A caller whose hash failed must get nothing rather than a stub that every other failure
	// would also produce, which would put unrelated players on one account.
	EXPECT_EQ("", AccountNameFromDigest(Bytes(4)));
	EXPECT_EQ("", AccountNameFromDigest(std::vector<unsigned char>()));
}

TEST(AccountName, ExactlyEnoughDigestIsAccepted)
{
	// The boundary: sixteen bytes is thirty-two hex characters, which is the whole name.
	EXPECT_EQ(32u, AccountNameFromDigest(Bytes(16)).size());
}

// ---------------------------------------------------------------- key paths

TEST(IdentityPaths, TheFirstInstanceGetsThePlainName)
{
	// The file a player is told to back up must be the one the documentation names.
	EXPECT_EQ(std::string(kRoot) + "/identity/client-account-auth.key", ClientAuthKeyPath(kRoot, 0));
}

TEST(IdentityPaths, FurtherInstancesAreNumberedFromTwo)
{
	// A second client on one machine cannot share the first one's account: the server refuses a
	// duplicate, because a duplicate normally means somebody has your key.
	EXPECT_EQ(std::string(kRoot) + "/identity/client-account-auth.2.key", ClientAuthKeyPath(kRoot, 1));
	EXPECT_EQ(std::string(kRoot) + "/identity/client-account-auth.3.key", ClientAuthKeyPath(kRoot, 2));
}

TEST(IdentityPaths, ANegativeInstanceIsTheFirstOne)
{
	EXPECT_EQ(ClientAuthKeyPath(kRoot, 0), ClientAuthKeyPath(kRoot, -1));
}

TEST(IdentityPaths, TheServerKeyIsOnePerMachine)
{
	// One per machine rather than per server, so an operator running several offers one account
	// namespace across all of them and one shared database.
	EXPECT_EQ(std::string(kRoot) + "/identity/server-account-auth.key", ServerAuthKeyPath(kRoot));
}

TEST(IdentityPaths, ATrailingSeparatorDoesNotDoubleUp)
{
	EXPECT_EQ(ClientAuthKeyPath(kRoot, 0), ClientAuthKeyPath(std::string(kRoot) + "/", 0));
	EXPECT_EQ(ServerAuthKeyPath(kRoot), ServerAuthKeyPath(std::string(kRoot) + "\\"));
}

TEST(IdentityPaths, NoRootMeansNoPath)
{
	EXPECT_EQ("", ClientAuthKeyPath("", 0));
	EXPECT_EQ("", ServerAuthKeyPath(""));
}

// ---------------------------------------------------------------- what gets signed

TEST(ProofMessage, TheTwoSidesNeverSignTheSameBytes)
{
	// Without separate domain tags a signature harvested from a server could be handed back to it
	// as a client's proof, which is authentication by echo.
	EXPECT_NE(ClientProofMessage("aabb", "ccdd"), ServerProofMessage("aabb", "ccdd"));
}

TEST(ProofMessage, CarriesTheSessionAndTheServerKey)
{
	const std::string msg = ClientProofMessage("5e5510", "5e2ve2");

	EXPECT_NE(std::string::npos, msg.find("5e5510"));
	EXPECT_NE(std::string::npos, msg.find("5e2ve2"));
}

TEST(ProofMessage, IsVersioned)
{
	// The tag is what lets the scheme change without old signatures being valid under the new one.
	EXPECT_NE(std::string::npos, ClientProofMessage("a", "b").find("v1"));
	EXPECT_NE(std::string::npos, ServerProofMessage("a", "b").find("v1"));
}

TEST(ProofMessage, FieldsCannotBeSlidIntoEachOther)
{
	// THE reason there is a separator. Session "ab" with key "cd" and session "a" with key "bcd"
	// are different facts, and without a delimiter they are the same bytes and one signature
	// covers both.
	EXPECT_NE(ClientProofMessage("ab", "cd"), ClientProofMessage("a", "bcd"));
	EXPECT_NE(ServerProofMessage("ab", "cd"), ServerProofMessage("a", "bcd"));
}

TEST(ProofMessage, ADifferentSessionIsADifferentMessage)
{
	// What makes a proof worthless outside the connection it was made on, which is what stops a
	// copied-public-key server relaying a live player's proof to the real one.
	EXPECT_NE(ClientProofMessage("session1", "key"), ClientProofMessage("session2", "key"));
}

TEST(ProofMessage, ADifferentServerIsADifferentMessage)
{
	EXPECT_NE(ClientProofMessage("session", "keyA"), ClientProofMessage("session", "keyB"));
}

// ---------------------------------------------------------------- where the keys live

TEST(IdentityRoot, HangsOffWhateverBaseItIsGiven)
{
	EXPECT_EQ("C:/Users/x/AppData/Local/ForkUnderA",
		IdentityRootUnder("C:/Users/x/AppData/Local"));
	EXPECT_EQ("/home/x/.config/ForkUnderA", IdentityRootUnder("/home/x/.config"));
}

TEST(IdentityRoot, ATrailingSeparatorOfEitherKindDoesNotDoubleUp)
{
	EXPECT_EQ(IdentityRootUnder("/home/x/.config"), IdentityRootUnder("/home/x/.config/"));
	EXPECT_EQ(IdentityRootUnder("D:/Games/Fua"), IdentityRootUnder("D:/Games/Fua\\"));
}

TEST(IdentityRoot, NoBaseMeansNoRoot)
{
	// How a caller whose OS would not say where the user's folder is refuses to invent one.
	EXPECT_EQ("", IdentityRootUnder(""));
	EXPECT_EQ("", IdentityRootUnder("/"));
	EXPECT_EQ("", IdentityRootUnder("\\\\"));
}

TEST(IdentityRoot, TheSameCallNamesTheOldFolderToMigrateFrom)
{
	// One function for both, since the old rule was this same append against the config directory.
	// The migration runs exactly when the two disagree.
	const std::string current = IdentityRootUnder("C:/Users/x/AppData/Local");
	const std::string legacy = IdentityRootUnder("D:/Games/Fua");

	EXPECT_NE(current, legacy);
	EXPECT_EQ("D:/Games/Fua/ForkUnderA", legacy);
}
