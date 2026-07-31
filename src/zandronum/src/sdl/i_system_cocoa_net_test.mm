// [rc4l] Regression test for the Mac_HttpsGet first-launch use-after-free (GlitchTip #127 / issue
// #122). i_system_cocoa_net.mm is compiled WITHOUT ARC, so the update-check completion handler must
// retain the NSData it captures; otherwise the handler's autorelease pool drains, the body is freed,
// and the later [body length]/[body bytes] read messages a dead object -- an intermittent crash that
// only surfaced on a fresh first launch and never reproduced under lldb.
//
// This drives the two extracted steps -- Mac_RetainBody then Mac_CopyBody -- across a REAL autorelease
// pool drain, with a locally-built NSData and no network. On the pre-fix code (a plain `body = data`,
// no retain) AddressSanitizer reports heap-use-after-free at Mac_CopyBody; with the retain it passes.
// The macOS-only test target zandrox_tests_objc builds this under ASan (see tests/CMakeLists.txt); the
// Linux/Windows test binaries never compile it.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#import <Foundation/Foundation.h>
#include <cstring>
#include "gtest/gtest.h"
#include "sdl/i_system_cocoa_net.h"

// The core regression: a body captured in a completion handler must survive that handler's autorelease
// pool draining. The reads happen AFTER the @autoreleasepool block closes -- exactly as Mac_HttpsGet
// reads after its semaphore wait, outside the handler. Without the retain, ASan traps here.
TEST(MacHttpsGetBody, SurvivesAutoreleasePoolDrain)
{
	__block NSData *snap = nil;
	@autoreleasepool
	{
		NSData *data = [NSData dataWithBytes:"hello" length:5]; // autoreleased, like NSURLSession's body
		snap = Mac_RetainBody( data );                         // the shipping capture path
	}                                                          // pool drains -> `data` freed unless retained

	char out[16] = { 0 };
	int n = Mac_CopyBody( snap, out, sizeof out );             // reads `snap` after the drain
	[snap release];

	EXPECT_EQ( n, 5 );
	EXPECT_STREQ( out, "hello" );
}

// An oversized body truncates to outSize-1 and stays NUL-terminated (the tag parser tolerates a body
// cut mid-response); the copy never over-runs the caller's buffer.
TEST(MacHttpsGetBody, TruncatesOversizedBody)
{
	NSData *data = [[NSData alloc] initWithBytes:"abcdef" length:6];
	char out[4] = { 0 };
	int n = Mac_CopyBody( data, out, sizeof out );
	[data release];

	EXPECT_EQ( n, 3 );
	EXPECT_STREQ( out, "abc" );
}

// An empty (0-byte) 2xx body copies to an empty, NUL-terminated string.
TEST(MacHttpsGetBody, HandlesEmptyBody)
{
	NSData *data = [NSData data];
	char out[8];
	std::memset( out, 'x', sizeof out );
	int n = Mac_CopyBody( data, out, sizeof out );

	EXPECT_EQ( n, 0 );
	EXPECT_STREQ( out, "" );
}

// A nil body or a non-positive buffer size never reads or writes out of bounds.
TEST(MacHttpsGetBody, HandlesNilAndZeroBuffer)
{
	char out[8];
	std::memset( out, 'x', sizeof out );
	EXPECT_EQ( Mac_CopyBody( nil, out, sizeof out ), 0 );
	EXPECT_STREQ( out, "" );                       // nil body still empties a usable buffer
	EXPECT_EQ( Mac_CopyBody( nil, out, 0 ), 0 );   // zero-size buffer: no write at all
}
