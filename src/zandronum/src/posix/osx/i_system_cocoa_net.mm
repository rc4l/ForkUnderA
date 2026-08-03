// [rc4l] macOS updater networking, split out of i_system_cocoa.mm so it carries NO SDL dependency and
// can be compiled straight into a unit-test target (i_system_cocoa_net_test.mm). Holds the synchronous
// HTTPS GET for the update check plus the two memory-management helpers whose omission caused the
// intermittent first-launch use-after-free (GlitchTip #127 / issue #122).
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#import <Foundation/Foundation.h>
#include <cstring>
#include "i_system_cocoa_net.h"

// [rc4l] This TU is compiled WITHOUT ARC (manual retain/release), matching the engine's build flags.
// Mac_RetainBody and Mac_CopyBody are the two steps that must straddle the completion handler's
// autorelease pool: the handler retains the body so it outlives the pool drain, and the copy-out runs
// afterward. They are extracted (rather than inlined) purely so the unit test can exercise the exact
// retain/release code path across a real pool drain -- if the retain is ever dropped again,
// AddressSanitizer traps in Mac_CopyBody instead of the bug shipping.

extern "C" NSData *Mac_RetainBody(NSData *data)
{
	// Take ownership so `data` survives past the completion handler's autorelease pool. Without this
	// retain the pool drains, `data` is freed, and the later Mac_CopyBody read messages a dead object.
	// Balanced by the [release] in Mac_HttpsGet after the bytes are copied out.
	return [data retain];
}

extern "C" int Mac_CopyBody(NSData *body, char *out, int outSize)
{
	if ( out == NULL || outSize <= 0 )
		return 0;
	out[0] = '\0';
	if ( body == nil )
		return 0;
	NSUInteger len = [body length];
	if ( len >= (NSUInteger)outSize )
		len = (NSUInteger)outSize - 1; // truncate an oversized body; the tag parser tolerates a cut
	memcpy( out, [body bytes], len );
	out[len] = '\0';
	return (int)len;
}

// [rc4l] Synchronous HTTPS GET for the auto-update check, via NSURLSession (system TLS + CA store).
// Runs on the updater's background thread; a semaphore makes it blocking with a hard time cap so a
// slow/hung server can never wedge the thread. Fills `out` with the response body on a 2xx and returns
// true; returns false (out emptied) on timeout, any transport error, or a non-2xx status. `urlStr`
// must be a full https URL. This is fail-safe by design: false simply means "we don't know", so the
// update notice stays hidden.
extern "C" bool Mac_HttpsGet(const char *urlStr, char *out, int outSize, int timeoutSecs)
{
	if ( out == NULL || outSize <= 0 )
		return false;
	out[0] = '\0';
	if ( urlStr == NULL )
		return false;

	@autoreleasepool
	{
		NSString *s = [NSString stringWithUTF8String:urlStr];
		NSURL *url = ( s != nil ) ? [NSURL URLWithString:s] : nil;
		if ( url == nil )
			return false;

		NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:url];
		[req setTimeoutInterval:(NSTimeInterval)timeoutSecs];
		[req setValue:@"ZandroX-updater" forHTTPHeaderField:@"User-Agent"]; // GitHub API requires a UA
		[req setValue:@"application/vnd.github+json" forHTTPHeaderField:@"Accept"];

		__block bool ok = false;
		__block NSData *body = nil;
		dispatch_semaphore_t sem = dispatch_semaphore_create( 0 );
		NSURLSessionDataTask *task = [[NSURLSession sharedSession] dataTaskWithRequest:req
			completionHandler:^(NSData *data, NSURLResponse *resp, NSError *err)
			{
				if ( err == nil && [resp isKindOfClass:[NSHTTPURLResponse class]] )
				{
					NSInteger code = [(NSHTTPURLResponse *)resp statusCode];
					if ( code >= 200 && code < 300 )
					{
						ok = true;
						body = Mac_RetainBody( data ); // retain across the pool drain (see helper)
					}
				}
				dispatch_semaphore_signal( sem );
			}];
		[task resume];

		// NSURLSession guarantees the completion handler runs exactly once (with data or a timeout error
		// from setTimeoutInterval), so the semaphore is always signalled. The deadline is a belt-and-
		// suspenders cap a bit past the request timeout so we can never block forever.
		dispatch_time_t deadline = dispatch_time( DISPATCH_TIME_NOW, (int64_t)( timeoutSecs + 2 ) * NSEC_PER_SEC );
		if ( dispatch_semaphore_wait( sem, deadline ) != 0 )
		{
			[task cancel];
			return false; // pathological: handler never fired
		}

		bool result = false;
		if ( ok && body != nil )
		{
			Mac_CopyBody( body, out, outSize );
			result = true;
		}
		[body release]; // balance the retain in the handler (nil-safe)
		return result;
	}
}
