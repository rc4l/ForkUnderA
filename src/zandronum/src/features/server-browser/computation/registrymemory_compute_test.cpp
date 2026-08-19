// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/server-browser/computation/registrymemory_compute.h"

#include <string>

using namespace zx;

namespace
{

std::string Recalled( const RegistryMemory &memory, const char *server )
{
	std::string out;
	return memory.Recall( server, out ) ? out : std::string( "<none>" );
}

} // namespace

TEST( RegistryMemory, RemembersWhoListedEachServer )
{
	// The federated case, which is the ordinary one: three registries, three servers, and only the
	// registry that listed a server can introduce anybody to it.
	RegistryMemory memory;
	memory.Remember( "1.2.3.4:10666", "registry.one:15300" );
	memory.Remember( "5.6.7.8:10666", "registry.two:15300" );
	memory.Remember( "[2001:db8::1]:10666", "registry.three:15300" );

	EXPECT_EQ( "registry.one:15300", Recalled( memory, "1.2.3.4:10666" ));
	EXPECT_EQ( "registry.two:15300", Recalled( memory, "5.6.7.8:10666" ));
	EXPECT_EQ( "registry.three:15300", Recalled( memory, "[2001:db8::1]:10666" ));
}

TEST( RegistryMemory, AServerNobodyMentionedIsNotRecalled )
{
	RegistryMemory memory;
	memory.Remember( "1.2.3.4:10666", "registry.one:15300" );

	std::string out;
	EXPECT_FALSE( memory.Recall( "9.9.9.9:10666", out ));
}

TEST( RegistryMemory, SeeingAServerAgainUpdatesInPlace )
{
	// A server in every refresh must not evict everything else, and a server that moved registries
	// must not keep answering with the old one.
	RegistryMemory memory;
	memory.Remember( "1.2.3.4:10666", "registry.one:15300" );
	memory.Remember( "1.2.3.4:10666", "registry.two:15300" );

	EXPECT_EQ( 1u, memory.Size( ));
	EXPECT_EQ( "registry.two:15300", Recalled( memory, "1.2.3.4:10666" ));
}

TEST( RegistryMemory, ThePortIsPartOfTheIdentity )
{
	// Two servers on one machine are two servers, and they can be listed by different registries.
	RegistryMemory memory;
	memory.Remember( "1.2.3.4:10666", "registry.one:15300" );
	memory.Remember( "1.2.3.4:10667", "registry.two:15300" );

	EXPECT_EQ( 2u, memory.Size( ));
	EXPECT_EQ( "registry.one:15300", Recalled( memory, "1.2.3.4:10666" ));
	EXPECT_EQ( "registry.two:15300", Recalled( memory, "1.2.3.4:10667" ));
}

TEST( RegistryMemory, AnUnnameableAddressIsNotStored )
{
	RegistryMemory memory;
	memory.Remember( "", "registry.one:15300" );
	memory.Remember( "1.2.3.4:10666", "" );

	EXPECT_EQ( 0u, memory.Size( ));
}

TEST( RegistryMemory, AFullBrowserListFitsWithNothingEvicted )
{
	// The capacity is the browser's own server limit, so an entire refresh survives.
	RegistryMemory memory;
	for ( size_t i = 0; i < RegistryMemory::kCapacity; ++i )
		memory.Remember( "server" + std::to_string( i ), "registry.one:15300" );

	EXPECT_EQ( RegistryMemory::kCapacity, memory.Size( ));
	EXPECT_EQ( "registry.one:15300", Recalled( memory, "server0" ));
}

TEST( RegistryMemory, PastCapacityTheOldestGoesAndTheNewestStays )
{
	RegistryMemory memory;
	for ( size_t i = 0; i < RegistryMemory::kCapacity + 2; ++i )
		memory.Remember( "server" + std::to_string( i ), "registry.one:15300" );

	EXPECT_EQ( RegistryMemory::kCapacity, memory.Size( ));

	std::string out;
	EXPECT_FALSE( memory.Recall( "server0", out )) << "the oldest should have gone";
	EXPECT_FALSE( memory.Recall( "server1", out ));
	EXPECT_TRUE( memory.Recall( "server2", out )) << "and nothing newer than it";
	EXPECT_TRUE( memory.Recall( "server" + std::to_string( RegistryMemory::kCapacity + 1 ), out ));
}

TEST( RegistryMemory, ClearingForgetsEverything )
{
	RegistryMemory memory;
	memory.Remember( "1.2.3.4:10666", "registry.one:15300" );
	memory.Clear( );

	EXPECT_EQ( 0u, memory.Size( ));
}

// ---------------------------------------------------------------- choosing

TEST( ChooseRegistry, TheOneThatListedItWins )
{
	// Even when a registry has just answered a refresh, it is not necessarily the one holding this
	// server, and only the one holding it can answer about it.
	EXPECT_EQ( RegistryChoice::Remembered, ChooseRegistry( true, true ));
	EXPECT_EQ( RegistryChoice::Remembered, ChooseRegistry( true, false ));
}

TEST( ChooseRegistry, WithoutAMemoryTheLastToAnswerIsTheBestGuess )
{
	// The old behaviour, kept as the fallback: a typed address was never listed by anybody, so there
	// is nothing to remember and the answering registry is all there is.
	EXPECT_EQ( RegistryChoice::Answering, ChooseRegistry( false, true ));
}

TEST( ChooseRegistry, WithNeitherWeDoNotAsk )
{
	EXPECT_EQ( RegistryChoice::None, ChooseRegistry( false, false ));
}
