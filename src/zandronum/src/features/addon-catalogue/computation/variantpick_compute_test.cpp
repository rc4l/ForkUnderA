// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/addon-catalogue/computation/variantpick_compute.h"

using zx::AddonEntry;
using zx::AddonVariant;
using zx::ComposeServerName;
using zx::kDefaultVariantCfg;
using zx::PickVariant;
using zx::VariantPick;

namespace
{

AddonVariant Variant( const char *id, const char *name, const char *cfg, bool bDefault = false )
{
	AddonVariant v;
	v.id = id;
	v.name = name;
	v.cfg = cfg;
	v.isDefault = bDefault;
	return v;
}

// Skulltag as it would actually be written: one pack, several ways to play it, the deathmatch one
// keeping server.cfg so an older build lands on the same thing.
AddonEntry Skulltag( )
{
	AddonEntry e;
	e.id = "skulltag";
	e.name = "Skulltag";
	e.valid = true;
	e.variants.push_back( Variant( "dm", "Deathmatch", "server.cfg", true ));
	e.variants.push_back( Variant( "duel", "Duel", "duel.cfg" ));
	e.variants.push_back( Variant( "ctf", "CTF", "ctf.cfg" ));
	e.variants.push_back( Variant( "invasion", "Invasion", "invasion.cfg" ));
	return e;
}

} // namespace

// ------------------------------------------------------- entries without variants

TEST( VariantPick, APackThatPlaysOneWayHasNothingToChoose )
{
	// Most entries. The caller draws no panel, and still gets a cfg to run.
	AddonEntry plain;
	plain.id = "duel40";
	plain.name = "Duel 40";

	const VariantPick pick = PickVariant( plain, "" );

	EXPECT_EQ( -1, pick.index );
	EXPECT_EQ( std::string( kDefaultVariantCfg ), pick.cfg );
	EXPECT_TRUE( pick.name.empty( ));
}

TEST( VariantPick, APreferenceForAPackWithNoVariantsIsHarmless )
{
	// A remembered choice can outlive the entry it belonged to, including one that never had
	// variants at all. It must not produce an index into an empty list.
	AddonEntry plain;
	plain.name = "Duel 40";

	const VariantPick pick = PickVariant( plain, "invasion" );

	EXPECT_EQ( -1, pick.index );
	EXPECT_EQ( std::string( kDefaultVariantCfg ), pick.cfg );
}

// ------------------------------------------------------------------ choosing

TEST( VariantPick, NoPreferenceGetsTheDefault )
{
	const VariantPick pick = PickVariant( Skulltag( ), "" );

	EXPECT_EQ( 0, pick.index );
	EXPECT_EQ( "Deathmatch", pick.name );
	EXPECT_EQ( "server.cfg", pick.cfg );
}

TEST( VariantPick, TheDefaultIsWhicheverClaimsItRatherThanTheFirstListed )
{
	// Reordering the array for display must not change what an unchosen entry plays.
	AddonEntry e = Skulltag( );
	e.variants[0].isDefault = false;
	e.variants[2].isDefault = true;

	const VariantPick pick = PickVariant( e, "" );

	EXPECT_EQ( 2, pick.index );
	EXPECT_EQ( "CTF", pick.name );
}

TEST( VariantPick, WhatThePlayerChoseIsWhatTheyGet )
{
	const VariantPick pick = PickVariant( Skulltag( ), "invasion" );

	EXPECT_EQ( 3, pick.index );
	EXPECT_EQ( "Invasion", pick.name );
	EXPECT_EQ( "invasion.cfg", pick.cfg );
}

TEST( VariantPick, EveryVariantIsReachableByItsOwnId )
{
	// Swept, because an off-by-one here hands the player a different game from the one they picked
	// and the panel would still look right.
	const AddonEntry e = Skulltag( );

	for ( size_t i = 0; i < e.variants.size( ); ++i )
	{
		const VariantPick pick = PickVariant( e, e.variants[i].id );

		EXPECT_EQ( static_cast<int>( i ), pick.index ) << "variant " << i;
		EXPECT_EQ( e.variants[i].name, pick.name ) << "variant " << i;
		EXPECT_EQ( e.variants[i].cfg, pick.cfg ) << "variant " << i;
	}
}

TEST( VariantPick, AChoiceTheCatalogueNoLongerOffersFallsBackRatherThanVanishing )
{
	// THE update case: the player picked a variant, the pack shipped a new addon.json without it,
	// and the panel must not come up empty or the entry unplayable.
	const VariantPick pick = PickVariant( Skulltag( ), "terminator" );

	EXPECT_EQ( 0, pick.index );
	EXPECT_EQ( "Deathmatch", pick.name );
}

TEST( VariantPick, WithNoDefaultClaimedTheFirstOneWins )
{
	// The file is allowed to claim none. Something still has to play, and the first listed is the
	// only answer that does not depend on how the array happens to be sorted afterwards.
	AddonEntry e = Skulltag( );
	for ( size_t i = 0; i < e.variants.size( ); ++i )
		e.variants[i].isDefault = false;

	const VariantPick pick = PickVariant( e, "" );

	EXPECT_EQ( 0, pick.index );
	EXPECT_EQ( "Deathmatch", pick.name );
}

// --------------------------------------------------------------- the name

TEST( VariantPick, TheServerNameSaysWhichWayItIsBeingPlayed )
{
	// A joiner reading a list cannot see the cfg. "Skulltag" alone tells them nothing about whether
	// they are joining an invasion or a duel, and finding out by joining is the cost this avoids.
	EXPECT_EQ( "Skulltag: Invasion (Fua)",
		ComposeServerName( "Skulltag", "Invasion", "Fua" ));
}

TEST( VariantPick, APackWithOneWayToPlayIsNotGivenAnEmptyQualifier )
{
	EXPECT_EQ( "Duel 40 (Fua)", ComposeServerName( "Duel 40", "", "Fua" ));
}

TEST( VariantPick, NoSuffixMeansNoBrackets )
{
	EXPECT_EQ( "Skulltag: Duel", ComposeServerName( "Skulltag", "Duel", "" ));
	EXPECT_EQ( "Skulltag", ComposeServerName( "Skulltag", "", "" ));
}

TEST( VariantPick, ANamelessEntryDoesNotLeadWithPunctuation )
{
	// Defensive: the parser refuses an entry with no name, so this can only arrive from a caller
	// that built one by hand. Leading with ": Duel" would look like a truncated string.
	EXPECT_EQ( "Duel (Fua)", ComposeServerName( "", "Duel", "Fua" ));
	EXPECT_EQ( "(Fua)", ComposeServerName( "", "", "Fua" ));
}
