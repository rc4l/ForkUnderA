// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-hosting/computation/hostsettings_compute.h"

using zx::ComputeClampedMaxPlayers;
using zx::ComputeHostDisplayName;
using zx::ComputeSavedCvars;
using zx::ComputeSettingScope;
using zx::SettingScope;

namespace
{

typedef std::vector<std::pair<std::string, std::string> > CvarList;

CvarList Pairs(const char *const *names, std::size_t count)
{
	CvarList out;
	for (std::size_t i = 0; i < count; ++i)
		out.push_back(std::make_pair(std::string(names[i]), std::string("1")));

	return out;
}

} // namespace

// ---------------------------------------------------------------- scope

TEST(HostSettings, AnOrdinaryServerCvarIsSavedWithThePreset)
{
	EXPECT_EQ(SettingScope::Preset, ComputeSettingScope("sv_hostname"));
	EXPECT_EQ(SettingScope::Preset, ComputeSettingScope("sv_maxclients"));
	EXPECT_EQ(SettingScope::Preset, ComputeSettingScope("sv_fua_serverregistry_announce"));
	EXPECT_EQ(SettingScope::Preset, ComputeSettingScope("dmflags2"));
}

// The whole reason this unit exists: a password must not reach the folder by any route.
TEST(HostSettings, EveryPasswordIsSessionScoped)
{
	EXPECT_EQ(SettingScope::Session, ComputeSettingScope("sv_password"));
	EXPECT_EQ(SettingScope::Session, ComputeSettingScope("sv_joinpassword"));
	EXPECT_EQ(SettingScope::Session, ComputeSettingScope("sv_rconpassword"));
}

TEST(HostSettings, AClientCvarBelongsToTheMachine)
{
	EXPECT_EQ(SettingScope::Machine, ComputeSettingScope("cl_fua_newhostport"));
	EXPECT_EQ(SettingScope::Machine, ComputeSettingScope("cl_"));
}

// [rc4l] The prefix test must not fire on a name that merely starts with part of it, nor run off
// the end of a name shorter than the prefix.
TEST(HostSettings, AShortOrPartialNameIsNotMistakenForAClientCvar)
{
	EXPECT_EQ(SettingScope::Preset, ComputeSettingScope("cl"));
	EXPECT_EQ(SettingScope::Preset, ComputeSettingScope("c"));
	EXPECT_EQ(SettingScope::Preset, ComputeSettingScope(""));
	EXPECT_EQ(SettingScope::Preset, ComputeSettingScope("clip_size"));
}

// ---------------------------------------------------------------- what a folder may hold

TEST(HostSettings, SavingKeepsThePresetCvarsInOrderAndDropsTheRest)
{
	static const char *const kNames[] =
	{
		"sv_hostname", "sv_password", "sv_maxclients", "cl_fua_newhostport",
		"sv_fua_serverregistry_announce",
	};

	const CvarList saved = ComputeSavedCvars(Pairs(kNames, 5));

	ASSERT_EQ(static_cast<std::size_t>(3), saved.size());
	EXPECT_EQ("sv_hostname", saved[0].first);
	EXPECT_EQ("sv_maxclients", saved[1].first);
	EXPECT_EQ("sv_fua_serverregistry_announce", saved[2].first);
	EXPECT_EQ("1", saved[0].second);
}

TEST(HostSettings, SavingNothingSavesNothing)
{
	EXPECT_TRUE(ComputeSavedCvars(CvarList()).empty());
}

// ---------------------------------------------------------------- the player limit

TEST(HostSettings, ThePlayerLimitIsCorrectedRatherThanTrusted)
{
	EXPECT_EQ(1, ComputeClampedMaxPlayers(0));
	EXPECT_EQ(1, ComputeClampedMaxPlayers(-9));
	EXPECT_EQ(1, ComputeClampedMaxPlayers(1));
	EXPECT_EQ(8, ComputeClampedMaxPlayers(8));
	EXPECT_EQ(64, ComputeClampedMaxPlayers(64));
	EXPECT_EQ(64, ComputeClampedMaxPlayers(65));
}

// ---------------------------------------------------------------- the name

TEST(HostSettings, ATypedNameIsTheName)
{
	EXPECT_EQ("Talha's game", ComputeHostDisplayName("Talha's game"));
}

TEST(HostSettings, AnEmptyOrBlankNameFallsBackToTheDefault)
{
	EXPECT_EQ("FUA Custom Server", ComputeHostDisplayName(""));
	EXPECT_EQ("FUA Custom Server", ComputeHostDisplayName("   "));
	EXPECT_EQ("FUA Custom Server", ComputeHostDisplayName("\t"));

	// The constant and the fallback are the same answer, not two that happen to agree today.
	EXPECT_EQ(zx::kFuaDefaultBuildServerName, ComputeHostDisplayName(""));
}

// [rc4l] A leading space is not a blank name, so it must not be thrown away with them.
TEST(HostSettings, ANameThatMerelyStartsWithASpaceIsKept)
{
	EXPECT_EQ(" ok", ComputeHostDisplayName(" ok"));
}
