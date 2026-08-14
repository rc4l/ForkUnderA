// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/port-mapping/computation/natpmp_compute.h"

using zx::BuildNatPmpAddressRequest;
using zx::BuildNatPmpMapRequest;
using zx::kNatPmpRequestSize;
using zx::kNatPmpResponseSize;
using zx::NatPmpReply;
using zx::NatPmpResultText;
using zx::ReadNatPmpMapReply;
using std::vector;

namespace
{
// A well-formed mapping reply: version 0, opcode 128+op, result 0, then epoch, ports and lifetime.
vector<unsigned char> Reply( bool tcp, int result, int internalPort, int externalPort, int lifetime )
{
	vector<unsigned char> out;
	out.push_back( 0 );
	out.push_back( static_cast<unsigned char>(( tcp ? 2 : 1 ) + 128 ));
	out.push_back( static_cast<unsigned char>(( result >> 8 ) & 0xff ));
	out.push_back( static_cast<unsigned char>( result & 0xff ));
	for ( int i = 0; i < 4; ++i )				// seconds since epoch, which we do not use
		out.push_back( 0 );
	out.push_back( static_cast<unsigned char>(( internalPort >> 8 ) & 0xff ));
	out.push_back( static_cast<unsigned char>( internalPort & 0xff ));
	out.push_back( static_cast<unsigned char>(( externalPort >> 8 ) & 0xff ));
	out.push_back( static_cast<unsigned char>( externalPort & 0xff ));
	out.push_back( static_cast<unsigned char>(( lifetime >> 24 ) & 0xff ));
	out.push_back( static_cast<unsigned char>(( lifetime >> 16 ) & 0xff ));
	out.push_back( static_cast<unsigned char>(( lifetime >> 8 ) & 0xff ));
	out.push_back( static_cast<unsigned char>( lifetime & 0xff ));
	return out;
}
} // namespace

// ---------------------------------------------------------------- the requests

TEST( NatPmpRequest, IsTwelveBytesInTheOrderTheProtocolSays )
{
	const vector<unsigned char> request = BuildNatPmpMapRequest( 10666, 10666, false, 3600 );

	ASSERT_EQ( kNatPmpRequestSize, request.size( ));

	EXPECT_EQ( 0, request[0] );			// version
	EXPECT_EQ( 1, request[1] );			// opcode 1 = map UDP
	EXPECT_EQ( 0, request[2] );			// reserved, must be zero
	EXPECT_EQ( 0, request[3] );

	// Big-endian on the wire whatever this machine is.
	EXPECT_EQ( 0x29, request[4] );		// 10666 >> 8
	EXPECT_EQ( 0xAA, request[5] );		// 10666 & 0xff
	EXPECT_EQ( 0x29, request[6] );
	EXPECT_EQ( 0xAA, request[7] );

	EXPECT_EQ( 0, request[8] );			// lifetime 3600
	EXPECT_EQ( 0, request[9] );
	EXPECT_EQ( 0x0E, request[10] );
	EXPECT_EQ( 0x10, request[11] );
}

TEST( NatPmpRequest, TcpIsADifferentOpcode )
{
	EXPECT_EQ( 2, BuildNatPmpMapRequest( 1, 1, true, 0 )[1] );
	EXPECT_EQ( 1, BuildNatPmpMapRequest( 1, 1, false, 0 )[1] );
}

TEST( NatPmpRequest, ANegativeLifetimeBecomesZeroRatherThanEnormous )
{
	// It is a 32-bit unsigned field: a negative int written straight in reads as decades.
	const vector<unsigned char> request = BuildNatPmpMapRequest( 1, 1, false, -5 );

	EXPECT_EQ( 0, request[8] );
	EXPECT_EQ( 0, request[9] );
	EXPECT_EQ( 0, request[10] );
	EXPECT_EQ( 0, request[11] );
}

TEST( NatPmpRequest, TheAddressRequestIsJustTwoBytes )
{
	const vector<unsigned char> request = BuildNatPmpAddressRequest( );

	ASSERT_EQ( 2u, request.size( ));
	EXPECT_EQ( 0, request[0] );
	EXPECT_EQ( 0, request[1] );
}

// ---------------------------------------------------------------- reading the reply

TEST( NatPmpReplyReading, ReadsAGoodOne )
{
	const NatPmpReply reply = ReadNatPmpMapReply( Reply( false, 0, 10666, 10666, 3600 ), false );

	ASSERT_TRUE( reply.valid );
	EXPECT_EQ( 0, reply.resultCode );
	EXPECT_EQ( 10666, reply.internalPort );
	EXPECT_EQ( 10666, reply.externalPort );
	EXPECT_EQ( 3600, reply.lifetimeSeconds );
}

TEST( NatPmpReplyReading, ReportsAPortTheRouterChoseInstead )
{
	// A gateway is allowed to hand back a different external port, and a client that ignored that
	// would advertise one nobody can reach.
	const NatPmpReply reply = ReadNatPmpMapReply( Reply( false, 0, 10666, 40001, 3600 ), false );

	ASSERT_TRUE( reply.valid );
	EXPECT_EQ( 40001, reply.externalPort );
}

TEST( NatPmpReplyReading, RefusesADatagramTooShortToHoldTheFields )
{
	// [rc4l] A short read accepted as a reply is how a mapping gets "confirmed" against a router
	// that said nothing of the sort -- every field is a fixed offset into this buffer.
	for ( size_t length = 0; length < kNatPmpResponseSize; ++length )
	{
		vector<unsigned char> truncated = Reply( false, 0, 1, 1, 1 );
		truncated.resize( length );

		EXPECT_FALSE( ReadNatPmpMapReply( truncated, false ).valid ) << length;
	}
}

TEST( NatPmpReplyReading, RefusesAVersionItDoesNotKnow )
{
	vector<unsigned char> bytes = Reply( false, 0, 1, 1, 1 );
	bytes[0] = 1;

	EXPECT_FALSE( ReadNatPmpMapReply( bytes, false ).valid );
}

TEST( NatPmpReplyReading, RefusesAnAnswerToADifferentQuestion )
{
	// [rc4l] The two mappings we make differ ONLY by protocol, and a router under load reorders and
	// duplicates datagrams. Accepting a UDP answer for the TCP question would confirm a mapping that
	// was never made.
	const vector<unsigned char> udpReply = Reply( false, 0, 1, 1, 1 );

	EXPECT_TRUE( ReadNatPmpMapReply( udpReply, false ).valid );
	EXPECT_FALSE( ReadNatPmpMapReply( udpReply, true ).valid );
}

TEST( NatPmpReplyReading, RefusesARequestOpcodeEchoedBack )
{
	// Responses are the request opcode PLUS 128. A device echoing the request unchanged is not
	// answering it.
	vector<unsigned char> bytes = Reply( false, 0, 1, 1, 1 );
	bytes[1] = 1;

	EXPECT_FALSE( ReadNatPmpMapReply( bytes, false ).valid );
}

TEST( NatPmpReplyReading, AFailureIsStillAValidReply )
{
	// Valid means "we could read it", not "it went well" -- the result code is how the router says
	// no, and discarding the message would lose the reason.
	const NatPmpReply reply = ReadNatPmpMapReply( Reply( true, 2, 0, 0, 0 ), true );

	ASSERT_TRUE( reply.valid );
	EXPECT_EQ( 2, reply.resultCode );
}

TEST( NatPmpReplyReading, ALongDatagramIsFineAsLongAsTheFieldsAreThere )
{
	vector<unsigned char> padded = Reply( false, 0, 10666, 10666, 60 );
	padded.push_back( 0xff );
	padded.push_back( 0xff );

	EXPECT_TRUE( ReadNatPmpMapReply( padded, false ).valid );
}

// ---------------------------------------------------------------- explaining a refusal

TEST( NatPmpResult, EveryCodeSaysSomething )
{
	for ( int code = -1; code <= 6; ++code )
	{
		const char *const text = NatPmpResultText( code );
		ASSERT_NE( static_cast<const char *>( NULL ), text ) << code;
		EXPECT_NE( '\0', text[0] ) << code;
	}
}

TEST( NatPmpResult, NamesTheOneAPlayerCanDoSomethingAbout )
{
	// Code 2 is the router having automatic port opening switched off -- which is a setting, not a
	// fault, and the only one worth pointing somebody at.
	EXPECT_STRNE( NatPmpResultText( 0 ), NatPmpResultText( 2 ));
	EXPECT_STRNE( NatPmpResultText( 2 ), NatPmpResultText( 99 ));
}
