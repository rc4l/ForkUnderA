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

zx::AddonFileRef Ref( const char *name )
{
	zx::AddonFileRef f;
	f.name = name;
	f.md5 = "41630bc75af4b51fe5d163fe4d434c6e";
	return f;
}

// Ghouls vs Humans: no shared base whatever, and a different wad on each way of playing.
AddonEntry Ghouls( )
{
	AddonEntry e;
	e.id = "gvh";
	e.name = "Ghouls vs Humans";
	e.valid = true;

	AddonVariant classic = Variant( "gvh", "Classic", "server.cfg", true );
	classic.files.push_back( Ref( "gvh.pk3" ));
	e.variants.push_back( classic );

	AddonVariant reborn = Variant( "gvhr", "Reborn", "reborn.cfg" );
	reborn.files.push_back( Ref( "gvhr.pk3" ));
	reborn.files.push_back( Ref( "gvhr-maps.pk3" ));
	e.variants.push_back( reborn );

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

// ------------------------------------------------------------ what it loads

TEST( VariantPick, APackThatPlaysOneWayLoadsItsOwnFiles )
{
	AddonEntry plain;
	plain.name = "Duel 40";
	plain.files.push_back( Ref( "duel40b.pk3" ));

	const VariantPick pick = PickVariant( plain, "" );

	ASSERT_EQ( 1u, pick.files.size( ));
	EXPECT_EQ( "duel40b.pk3", pick.files[0].name );
}

TEST( VariantPick, AVariantsFilesComeAfterTheEntrysRatherThanInsteadOfThem )
{
	// [rc4l] ADDED, never replacing. A variant that restated the shared files would hold a copy of
	// them, and copies drift: update the base, miss one variant, and it quietly loads something else.
	AddonEntry e = Skulltag( );
	e.files.push_back( Ref( "skulltag.pk3" ));
	e.variants[3].files.push_back( Ref( "announcer.pk3" ));

	const VariantPick pick = PickVariant( e, "invasion" );

	ASSERT_EQ( 2u, pick.files.size( ));
	EXPECT_EQ( "skulltag.pk3", pick.files[0].name ) << "the base loads first";
	EXPECT_EQ( "announcer.pk3", pick.files[1].name );
}

TEST( VariantPick, VariantsThatDifferByCfgAloneAllLoadTheSameThing )
{
	// Skulltag's shape. Every way of playing gets the entry's list and nothing else.
	AddonEntry e = Skulltag( );
	e.files.push_back( Ref( "skulltag.pk3" ));

	for ( size_t i = 0; i < e.variants.size( ); ++i )
	{
		const VariantPick pick = PickVariant( e, e.variants[i].id );

		ASSERT_EQ( 1u, pick.files.size( )) << "variant " << i;
		EXPECT_EQ( "skulltag.pk3", pick.files[0].name ) << "variant " << i;
	}
}

TEST( VariantPick, WithNoSharedBaseEachWayOfPlayingLoadsOnlyItsOwn )
{
	// THE Ghouls case: picking the wrong list here starts a server on somebody else's wad, and the
	// panel beside it would still read correctly.
	const VariantPick classic = PickVariant( Ghouls( ), "gvh" );
	ASSERT_EQ( 1u, classic.files.size( ));
	EXPECT_EQ( "gvh.pk3", classic.files[0].name );

	const VariantPick reborn = PickVariant( Ghouls( ), "gvhr" );
	ASSERT_EQ( 2u, reborn.files.size( ));
	EXPECT_EQ( "gvhr.pk3", reborn.files[0].name );
	EXPECT_EQ( "gvhr-maps.pk3", reborn.files[1].name );
}

TEST( VariantPick, TheFallbackCarriesTheDefaultsFilesAndNotTheAskedForOnes )
{
	// A remembered choice the catalogue no longer offers falls back to the default -- and the files
	// have to fall back WITH it, or the server runs the default's cfg on a missing variant's wads.
	const VariantPick pick = PickVariant( Ghouls( ), "terminator" );

	EXPECT_EQ( 0, pick.index );
	ASSERT_EQ( 1u, pick.files.size( ));
	EXPECT_EQ( "gvh.pk3", pick.files[0].name );
}

TEST( VariantPick, AnEntryWithNoFilesAndNoVariantFilesLoadsNothing )
{
	// Defensive: the parser refuses this shape, so it can only arrive from a caller that built one by
	// hand. The answer is an empty list rather than a guess at what was meant.
	const VariantPick pick = PickVariant( Skulltag( ), "duel" );

	EXPECT_TRUE( pick.files.empty( ));
}

// ------------------------------------------------------------ where it opens

TEST( VariantPick, AVariantOpensWhereTheEntryDoesUnlessItSaysOtherwise )
{
	AddonEntry e = Skulltag( );
	e.map = "D2DM1";

	const VariantPick pick = PickVariant( e, "duel" );

	EXPECT_EQ( "D2DM1", pick.map ) << "a variant with no map of its own inherits the entry's";
}

TEST( VariantPick, AVariantsOwnMapWins )
{
	// [rc4l] THE case this exists for: three invasion packs under one entry open on alinv01, MAP01
	// and Z1INV01, and one entry-level answer cannot be all three.
	AddonEntry e = Skulltag( );
	e.map = "D2DM1";
	e.variants[3].map = "alinv01";

	const VariantPick pick = PickVariant( e, "invasion" );

	EXPECT_EQ( "alinv01", pick.map );
}

TEST( VariantPick, EveryVariantsMapIsResolvedNotJustTheDefaults )
{
	// Swept, because a map that only resolved for the default would look right on the entry you
	// happened to test and start the wrong one everywhere else.
	AddonEntry e = Skulltag( );
	e.map = "ENTRY";

	for ( size_t i = 0; i < e.variants.size( ); ++i )
		e.variants[i].map = ( i % 2 == 0 ) ? "" : "OWN";

	for ( size_t i = 0; i < e.variants.size( ); ++i )
	{
		const VariantPick pick = PickVariant( e, e.variants[i].id );
		EXPECT_EQ( ( i % 2 == 0 ) ? "ENTRY" : "OWN", pick.map ) << "variant " << i;
	}
}

TEST( VariantPick, APackThatPlaysOneWayStillAnswersWithItsMap )
{
	AddonEntry plain;
	plain.name = "Duel 40";
	plain.map = "START";
	plain.files.push_back( Ref( "duel40b.pk3" ));

	EXPECT_EQ( "START", PickVariant( plain, "" ).map );
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
