// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-hosting/computation/hostargs_compute.h"

using zx::BuildHostArgs;
using zx::HostConfig;
using zx::IsBareFileName;
using zx::IsSafeArgValue;
using zx::IsSafeFilePath;
using zx::IsUsablePort;
using zx::JoinWindowsCommandLine;
using zx::QuoteWindowsArg;
using zx::ResolveHostPort;
using std::string;
using std::vector;

namespace
{
// The index of `flag` in `args`, or -1. Used rather than eyeballing positions, because the order of
// the middle of the command line is not something a test should be pinning down.
int IndexOf(const vector<string> &args, const string &flag)
{
	for (size_t i = 0; i < args.size(); ++i)
	{
		if (args[i] == flag)
			return static_cast<int>(i);
	}
	return -1;
}

bool Has(const vector<string> &args, const string &flag)
{
	return IndexOf(args, flag) >= 0;
}

// The value that follows `flag`, or "" if the flag is absent or trailing.
string ValueAfter(const vector<string> &args, const string &flag)
{
	const int at = IndexOf(args, flag);
	if ((at < 0) || (static_cast<size_t>(at) + 1 >= args.size()))
		return "";
	return args[at + 1];
}

HostConfig Basic()
{
	HostConfig config;
	config.hostName = "Test Server";
	config.iwad = "freedoom2.wad";
	config.map = "map01";
	config.maxPlayers = 8;
	config.port = 10666;
	return config;
}
} // namespace

// ---------------------------------------------------------------- what may be a value

TEST(HostArgValue, AcceptsOrdinaryNames)
{
	EXPECT_TRUE(IsSafeArgValue("map01"));
	EXPECT_TRUE(IsSafeArgValue("Bob's Server"));
	EXPECT_TRUE(IsSafeArgValue("freedoom2.wad"));
	EXPECT_TRUE(IsSafeArgValue("a"));
}

TEST(HostArgValue, RejectsAnythingThatWouldReadAsAnotherFlag)
{
	// The engine's argument parser reads position. A value beginning with a dash is not a value any
	// more, it is the next option -- and `-iwad` as a "map name" would change which game loads.
	EXPECT_FALSE(IsSafeArgValue("-host"));
	EXPECT_FALSE(IsSafeArgValue("-iwad"));
	EXPECT_FALSE(IsSafeArgValue("+exec"));
}

TEST(HostArgValue, RejectsQuotesAndBackslashes)
{
	// These are how a value escapes its own argument once Windows re-joins the vector into a string.
	EXPECT_FALSE(IsSafeArgValue("say \"hi\""));
	EXPECT_FALSE(IsSafeArgValue("C:\\wads\\x.wad"));
}

TEST(HostArgValue, RejectsControlCharacters)
{
	// A newline ends one line of a config file and begins another.
	EXPECT_FALSE(IsSafeArgValue("map01\nquit"));
	EXPECT_FALSE(IsSafeArgValue("map01\ttab"));
	EXPECT_FALSE(IsSafeArgValue(string("nul\0byte", 8)));
	EXPECT_FALSE(IsSafeArgValue("del\x7f"));
}

TEST(HostArgValue, RejectsEmpty)
{
	EXPECT_FALSE(IsSafeArgValue(""));
}

// ---------------------------------------------------------------- what may be a filename

TEST(HostFileName, AcceptsABareName)
{
	EXPECT_TRUE(IsBareFileName("maps_ep1.wad"));
	EXPECT_TRUE(IsBareFileName("a.pk3"));
}

TEST(HostFileName, RejectsAnythingWithAPathInIt)
{
	// The engine resolves WADs from its own search path. A path here either came from confusion or
	// from someone hoping to load something that is not a WAD at all.
	EXPECT_FALSE(IsBareFileName("../secrets.wad"));
	EXPECT_FALSE(IsBareFileName("sub/dir.wad"));
	EXPECT_FALSE(IsBareFileName("C:/x.wad"));
	EXPECT_FALSE(IsBareFileName("."));
	EXPECT_FALSE(IsBareFileName(".."));
	EXPECT_FALSE(IsBareFileName("a..b.wad"));
}

TEST(HostFileName, RejectsAColonEvenWithNoSlashInSight)
{
	// [rc4l] Two things wear a colon and neither is a filename. `C:x.wad` is a path relative to the
	// current directory OF THAT DRIVE -- it looks bare and is not. `map.wad:hidden` names an NTFS
	// alternate data stream, which is a way to read something that is not the file it appears to be.
	//
	// Its own test because the obvious case, `C:/x.wad`, carries a slash and is caught one line
	// earlier -- so the colon rule can look covered while never having fired.
	EXPECT_FALSE(IsBareFileName("C:x.wad"));
	EXPECT_FALSE(IsBareFileName("map.wad:hidden"));
}

TEST(HostFileName, RejectsWhatIsNotASafeValueEither)
{
	// Backslash separators are caught by the value rule before the path rule sees them, and either
	// answer is the same one.
	EXPECT_FALSE(IsBareFileName("dir\\x.wad"));
	EXPECT_FALSE(IsBareFileName(""));
	EXPECT_FALSE(IsBareFileName("-file"));
}

// ---------------------------------------------------------------- the command line

TEST(HostArgs, LeadsWithTheExecutableAndTheFlagThatDefinesTheRun)
{
	const vector<string> args = BuildHostArgs("zandronum.exe", Basic());

	ASSERT_FALSE(args.empty());
	EXPECT_EQ("zandronum.exe", args[0]);
	EXPECT_TRUE(Has(args, "-host"));
}

TEST(HostArgs, NeverAsksTheEngineToFindItsOwnStdout)
{
	// [rc4l] -stdout looks like exactly what a parent reading a pipe would want, and is the opposite.
	// Its probe fails on an anonymous pipe, and the fallback is AllocConsole -- a console window on
	// the desktop of a player who asked for a headless server. The child writes to its inherited
	// handle directly instead.
	EXPECT_FALSE(Has(BuildHostArgs("z", Basic()), "-stdout"));
}

TEST(HostArgs, TellsTheChildWhoToDieWith)
{
	HostConfig config = Basic();
	config.parentPid = 4242;
	EXPECT_EQ("4242", ValueAfter(BuildHostArgs("z", config), "-fua_hostparent"));

	// A server a person started themselves has no parent to watch, and must not adopt one.
	config.parentPid = 0;
	EXPECT_FALSE(Has(BuildHostArgs("z", config), "-fua_hostparent"));
}

TEST(HostArgs, AsksForNoWindowUnlessSomebodyWantsOne)
{
	EXPECT_TRUE(Has(BuildHostArgs("z", Basic()), "-fua_hidden"));

	HostConfig visible = Basic();
	visible.hideWindow = false;
	EXPECT_FALSE(Has(BuildHostArgs("z", visible), "-fua_hidden"));
}

TEST(HostArgs, CarriesTheThingsThePlayerChose)
{
	HostConfig config = Basic();
	config.pwads.push_back("one.wad");
	config.pwads.push_back("two.wad");

	const vector<string> args = BuildHostArgs("z", config);

	EXPECT_EQ("freedoom2.wad", ValueAfter(args, "-iwad"));
	EXPECT_EQ("map01", ValueAfter(args, "+map"));
	EXPECT_EQ("Test Server", ValueAfter(args, "+sv_hostname"));
	EXPECT_EQ("10666", ValueAfter(args, "-port"));
	EXPECT_EQ("8", ValueAfter(args, "+sv_maxclients"));

	// Two files means two -file flags, in the order given: WAD load order is not commutative.
	int seen = 0;
	for (size_t i = 0; i + 1 < args.size(); ++i)
	{
		if (args[i] != "-file")
			continue;
		EXPECT_EQ((seen == 0) ? "one.wad" : "two.wad", args[i + 1]);
		++seen;
	}
	EXPECT_EQ(2, seen);
}

TEST(HostArgs, DropsAHostileValueRatherThanPlacingIt)
{
	// A name that would read as a flag is dropped, and the rest of the command line is unharmed --
	// the player gets a server with a default name, not a server running someone else's arguments.
	HostConfig config = Basic();
	config.hostName = "-exec autoexec.cfg";

	const vector<string> args = BuildHostArgs("z", config);

	EXPECT_FALSE(Has(args, "+sv_hostname"));
	EXPECT_FALSE(Has(args, "-exec autoexec.cfg"));
	EXPECT_EQ("map01", ValueAfter(args, "+map"));
}

TEST(HostArgs, DropsAPathedWadAndKeepsTheRest)
{
	HostConfig config = Basic();
	config.pwads.push_back("../evil.wad");
	config.pwads.push_back("fine.wad");

	const vector<string> args = BuildHostArgs("z", config);

	EXPECT_EQ("fine.wad", ValueAfter(args, "-file"));
	for (size_t i = 0; i < args.size(); ++i)
		EXPECT_NE("../evil.wad", args[i]);
}

TEST(HostArgs, OmitsTheIwadRatherThanPassingSomethingUnusable)
{
	HostConfig config = Basic();
	config.iwad = "../../doom2.wad";

	const vector<string> args = BuildHostArgs("z", config);

	EXPECT_FALSE(Has(args, "-iwad"));
}

TEST(HostArgs, OmitsTheMapWhenThereIsNotOne)
{
	HostConfig config = Basic();
	config.map = "";

	EXPECT_FALSE(Has(BuildHostArgs("z", config), "+map"));
}

TEST(HostArgs, APasswordIsAlsoEnforced)
{
	// Setting sv_password without sv_forcepassword produces a server that shows a padlock in the
	// browser and lets anybody in -- worse than no password, because it is believed.
	HostConfig config = Basic();
	config.password = "letmein";

	const vector<string> args = BuildHostArgs("z", config);

	EXPECT_EQ("letmein", ValueAfter(args, "+sv_password"));
	EXPECT_EQ("1", ValueAfter(args, "+sv_forcepassword"));
}

TEST(HostArgs, NoPasswordMeansNeitherFlag)
{
	const vector<string> args = BuildHostArgs("z", Basic());

	EXPECT_FALSE(Has(args, "+sv_password"));
	EXPECT_FALSE(Has(args, "+sv_forcepassword"));
}

TEST(HostArgs, AJoinPasswordIsEnforcedTheSameWay)
{
	HostConfig config = Basic();
	config.joinPassword = "spectate";

	const vector<string> args = BuildHostArgs("z", config);

	EXPECT_EQ("spectate", ValueAfter(args, "+sv_joinpassword"));
	EXPECT_EQ("1", ValueAfter(args, "+sv_forcejoinpassword"));
}

TEST(HostArgs, CarriesTheRconSecretWhenThereIsOne)
{
	HostConfig config = Basic();
	config.rconSecret = "a1b2c3d4e5f6";

	EXPECT_EQ("a1b2c3d4e5f6", ValueAfter(BuildHostArgs("z", config), "+sv_rconpassword"));
}

TEST(HostArgs, NoSecretMeansNoRconPasswordAtAll)
{
	// An empty sv_rconpassword must never be SET rather than omitted: on some builds an empty
	// password is one anybody can guess.
	EXPECT_FALSE(Has(BuildHostArgs("z", Basic()), "+sv_rconpassword"));
}

TEST(HostArgs, AdvertisingIsExplicitInBothDirections)
{
	// Always stated, never left to whatever the config file happened to hold -- "local" hosting that
	// silently announced itself would be a privacy failure, not a bug.
	HostConfig quiet = Basic();
	quiet.advertise = false;
	EXPECT_EQ("0", ValueAfter(BuildHostArgs("z", quiet), "+sv_updatemaster"));

	HostConfig loud = Basic();
	loud.advertise = true;
	EXPECT_EQ("1", ValueAfter(BuildHostArgs("z", loud), "+sv_updatemaster"));
}

TEST(HostArgs, ServingOurOwnFilesIsExplicitInBothDirections)
{
	HostConfig off = Basic();
	off.serveWads = false;
	EXPECT_EQ("0", ValueAfter(BuildHostArgs("z", off), "+sv_fua_download"));

	HostConfig on = Basic();
	on.serveWads = true;
	EXPECT_EQ("1", ValueAfter(BuildHostArgs("z", on), "+sv_fua_download"));
}

TEST(HostArgs, AGameModeIsOnlySetWhenChosen)
{
	HostConfig unset = Basic();
	unset.gameMode = -1;
	EXPECT_FALSE(Has(BuildHostArgs("z", unset), "+gamemode"));

	HostConfig chosen = Basic();
	chosen.gameMode = 4;
	EXPECT_EQ("4", ValueAfter(BuildHostArgs("z", chosen), "+gamemode"));
}

TEST(HostArgs, EveryFlagGetsItsValue)
{
	// A flag left trailing would swallow whatever the next thing on the line happened to be. Sweeping
	// it because the failure is silent: the server starts, just not the one that was asked for.
	HostConfig config = Basic();
	config.pwads.push_back("a.wad");
	config.password = "p";
	config.joinPassword = "j";
	config.rconSecret = "s";
	config.gameMode = 2;

	const vector<string> args = BuildHostArgs("z", config);

	for (size_t i = 0; i < args.size(); ++i)
	{
		const bool isFlag = !args[i].empty() && ((args[i][0] == '-') || (args[i][0] == '+'));
		if (!isFlag || (args[i] == "-host") || (args[i] == "-fua_hidden"))
			continue;

		ASSERT_LT(i + 1, args.size()) << args[i] << " has no value";
		EXPECT_FALSE(args[i + 1].empty()) << args[i];
	}
}

// ---------------------------------------------------------------- Windows quoting

TEST(WindowsQuoting, LeavesAnOrdinaryArgumentAlone)
{
	// Easier to read in a process listing, which is where anyone debugging this will be looking.
	EXPECT_EQ("map01", QuoteWindowsArg("map01"));
	EXPECT_EQ("-host", QuoteWindowsArg("-host"));
}

TEST(WindowsQuoting, QuotesWhitespace)
{
	EXPECT_EQ("\"Test Server\"", QuoteWindowsArg("Test Server"));
	EXPECT_EQ("\"a\tb\"", QuoteWindowsArg("a\tb"));
}

TEST(WindowsQuoting, QuotesTheEmptyArgumentSoItSurvives)
{
	// Without quotes an empty argument vanishes, and every argument after it shifts down one.
	EXPECT_EQ("\"\"", QuoteWindowsArg(""));
}

TEST(WindowsQuoting, BackslashesAreOnlySpecialBeforeAQuote)
{
	// [rc4l] The rule everyone implements wrongly from memory. Doubling backslashes everywhere turns
	// C:\wads\ into C:\\wads\\ in the child's argv, and the child then cannot find anything.
	EXPECT_EQ("C:\\wads\\x.wad", QuoteWindowsArg("C:\\wads\\x.wad"));
	EXPECT_EQ("\"C:\\my wads\\x.wad\"", QuoteWindowsArg("C:\\my wads\\x.wad"));
}

TEST(WindowsQuoting, DoublesTheBackslashesThatPrecedeAQuote)
{
	EXPECT_EQ("\"a\\\\\\\"b\"", QuoteWindowsArg("a\\\"b"));
	EXPECT_EQ("\"\\\"\"", QuoteWindowsArg("\""));
}

TEST(WindowsQuoting, DoublesATrailingRunBecauseTheClosingQuoteFollowsIt)
{
	// `a\` inside quotes would escape the closing quote and swallow the rest of the command line.
	EXPECT_EQ("\"a b\\\\\"", QuoteWindowsArg("a b\\"));
}

TEST(WindowsCommandLine, JoinsWithSingleSpaces)
{
	vector<string> args;
	args.push_back("z.exe");
	args.push_back("-host");
	args.push_back("Test Server");

	EXPECT_EQ("z.exe -host \"Test Server\"", JoinWindowsCommandLine(args));
}

TEST(WindowsCommandLine, AnEmptyVectorIsAnEmptyLine)
{
	EXPECT_EQ("", JoinWindowsCommandLine(vector<string>()));
}

// ---------------------------------------------------------------- ports

TEST(HostPort, RejectsThePrivilegedRange)
{
	// Nothing below 1024 binds without elevation on any platform we ship to, so offering it only
	// produces a failure the player cannot do anything about.
	EXPECT_FALSE(IsUsablePort(80));
	EXPECT_FALSE(IsUsablePort(1023));
	EXPECT_TRUE(IsUsablePort(1024));
}

TEST(HostPort, RejectsWhatIsNotAPortAtAll)
{
	EXPECT_FALSE(IsUsablePort(0));
	EXPECT_FALSE(IsUsablePort(-1));
	EXPECT_FALSE(IsUsablePort(65536));
	EXPECT_TRUE(IsUsablePort(65535));
}

TEST(HostPort, ZeroMeansTakeTheDefault)
{
	EXPECT_EQ(10666, ResolveHostPort(0, 10666));
	EXPECT_EQ(10666, ResolveHostPort(-5, 10666));
}

TEST(HostPort, AUsableRequestIsHonoured)
{
	EXPECT_EQ(20000, ResolveHostPort(20000, 10666));
}

TEST(HostPort, AnUnusableDefaultStillYieldsSomethingBindable)
{
	// A caller passing a privileged default is a programming error, but returning it would produce a
	// bind failure the player gets blamed for.
	EXPECT_TRUE(IsUsablePort(ResolveHostPort(0, 80)));
}

// ---------------------------------------------------------------- a catalogue entry's server.cfg

TEST(HostArgs, ExecsACatalogueEntrysConfig)
{
	HostConfig config = Basic();
	config.execCfg = "F:/ZandroX/catalogue/duel40/server.cfg";

	const vector<string> args = BuildHostArgs("z", config);

	ASSERT_TRUE(Has(args, "+exec"));
	EXPECT_EQ("F:/ZandroX/catalogue/duel40/server.cfg", ValueAfter(args, "+exec"));
}

TEST(HostArgs, TheConfigIsExecdBeforeTheMapIsChosen)
{
	// The server applies these in order, and an entry's cfg is a pile of addmap lines. Exec'ing it
	// after +map would let the entry decide where the host lands instead of the host.
	HostConfig config = Basic();
	config.execCfg = "catalogue/duel40/server.cfg";

	const vector<string> args = BuildHostArgs("z", config);

	ASSERT_TRUE(Has(args, "+exec"));
	ASSERT_TRUE(Has(args, "+map"));
	EXPECT_LT(IndexOf(args, "+exec"), IndexOf(args, "+map"));
}

TEST(HostArgs, NoConfigMeansNoExecAtAll)
{
	// Most hosts are the manual form, which has no entry and therefore no cfg. An empty +exec would
	// be the server trying to read a file called nothing.
	EXPECT_FALSE(Has(BuildHostArgs("z", Basic()), "+exec"));
}

TEST(HostArgs, AConfigPathIsAllowedToBeAPathButNotAnArgument)
{
	// Unlike a WAD name this is deliberately a path, since the file lives in the entry's own folder.
	// What it still may not be is something that reads as another flag.
	HostConfig config = Basic();

	config.execCfg = "-host";
	EXPECT_FALSE(Has(BuildHostArgs("z", config), "+exec")) << "a value that reads as a flag";

	config.execCfg = "cat/a b/server.cfg";
	EXPECT_TRUE(Has(BuildHostArgs("z", config), "+exec")) << "a space is fine; args are a vector";

	config.execCfg = "cat/\"quoted\"/server.cfg";
	EXPECT_FALSE(Has(BuildHostArgs("z", config), "+exec")) << "a quote is not";
}

// ---------------------------------------------------------------- the settings menu wins

TEST(HostArgs, TheSettingsMenuBeatsTheExperienceConfig)
{
	// [rc4l] THE RULE, pinned. An experience says what to PLAY; the settings menu says how to RUN
	// it, and where the two speak about the same thing the menu wins.
	//
	// The engine applies arguments left to right and the last one wins, so the rule reduces to a
	// single fact about ordering: +exec is the FIRST '+' argument. Asserted that way on purpose
	// rather than as a list of the flags it must precede -- the list grows every time a setting is
	// added to the form, and a list would go stale silently while this cannot. Anything appended
	// later is after the exec by construction and therefore overrides the cfg for free.
	HostConfig config = Basic();
	config.execCfg = "catalogue/eoncollection/server.cfg";
	config.map = "DBAB01";
	config.hostName = "someone's server";
	config.maxPlayers = 8;
	config.password = "pw";
	config.joinPassword = "jp";
	config.rconSecret = "rc";
	config.gameMode = 2;

	const vector<string> args = BuildHostArgs("z", config);

	const int exec = IndexOf(args, "+exec");
	ASSERT_GE(exec, 0) << "the config has to be exec'd at all";

	for (size_t i = 0; i < args.size(); ++i)
	{
		if (args[i].empty() || args[i][0] != '+')
			continue;

		EXPECT_GE(static_cast<int>(i), exec)
			<< args[i] << " is set BEFORE the experience config is exec'd, so the config overrides "
			<< "it and the settings menu silently loses";
	}
}

TEST(HostArgs, APlayerLimitInTheConfigDoesNotBeatTheForm)
{
	// The concrete case that prompted the rule: a pasted config carrying sv_maxclients. Ours is
	// applied after the exec, so the number typed into the form is the one that survives.
	HostConfig config = Basic();
	config.execCfg = "catalogue/eoncollection/server.cfg";
	config.maxPlayers = 8;

	const vector<string> args = BuildHostArgs("z", config);

	EXPECT_GT(IndexOf(args, "+sv_maxclients"), IndexOf(args, "+exec"));
	EXPECT_GT(IndexOf(args, "+sv_maxplayers"), IndexOf(args, "+exec"));
	EXPECT_EQ("8", ValueAfter(args, "+sv_maxplayers"));
}

// ---------------------------------------------------------------- WADs go over as PATHS

TEST(SafeFilePath, AcceptsAResolvedAbsolutePath)
{
	// [rc4l] The shape that matters: what FindFileInEngineSearchPaths hands back on Windows, drive
	// letter and all. IsBareFileName refuses this on purpose, which is why the WAD arguments no
	// longer use it.
	EXPECT_TRUE(IsSafeFilePath("F:/ZandroX/dist-windows/Downloads/skulltag_content.pk3"));
	EXPECT_FALSE(IsBareFileName("F:/ZandroX/dist-windows/Downloads/skulltag_content.pk3"));
}

TEST(SafeFilePath, StillAcceptsABareName)
{
	// The fallback for when nothing resolved: hand over the name and let the server try its search.
	EXPECT_TRUE(IsSafeFilePath("duel40b.pk3"));
}

TEST(SafeFilePath, RefusesTraversalAndAnythingThatStopsBeingAValue)
{
	EXPECT_FALSE(IsSafeFilePath("wads/../../etc/passwd"));
	EXPECT_FALSE(IsSafeFilePath("-host"));
	EXPECT_FALSE(IsSafeFilePath("+map"));
	EXPECT_FALSE(IsSafeFilePath(""));
	EXPECT_FALSE(IsSafeFilePath("a\"b.pk3"));
	EXPECT_FALSE(IsSafeFilePath("a\\b.pk3"));
	EXPECT_FALSE(IsSafeFilePath("a\nb.pk3"));
}

TEST(HostArgs, HandsTheServerResolvedPathsRatherThanNames)
{
	// [rc4l] The bug this fixes. A bare name makes the SERVER search, and it searches its own config
	// -- not the one this client just registered a download folder in. So a pk3 we had only just
	// downloaded was invisible to the server it was downloaded for: the server started without it,
	// came up with one PWAD instead of two, and the client that joined was told its lumps did not
	// match.
	HostConfig config = Basic();
	config.iwad = "F:/wads/freedoom2.wad";
	config.pwads.clear();
	config.pwads.push_back("F:/ZandroX/dist-windows/Downloads/skulltag_content.pk3");
	config.pwads.push_back("zandrospree2rc2.pk3");

	const vector<string> args = BuildHostArgs("z", config);

	EXPECT_EQ("F:/wads/freedoom2.wad", ValueAfter(args, "-iwad"));

	// Both survive: the resolved one and the bare fallback.
	int seen = 0;
	for (size_t i = 0; i < args.size(); ++i)
	{
		if (args[i] == "-file")
			++seen;
	}
	EXPECT_EQ(2, seen) << "every wanted PWAD reached the command line";
}

TEST(HostArgs, ADangerousWadPathIsStillDropped)
{
	// Dropped rather than escaped, the same as every other unsafe value here.
	HostConfig config = Basic();
	config.iwad = "-host";
	config.pwads.clear();
	config.pwads.push_back("wads/../../secret.pk3");

	const vector<string> args = BuildHostArgs("z", config);

	EXPECT_FALSE(Has(args, "-iwad"));
	EXPECT_FALSE(Has(args, "-file"));
}

