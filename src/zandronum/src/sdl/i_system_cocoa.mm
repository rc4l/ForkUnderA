#include <CoreFoundation/CoreFoundation.h>
#import <AppKit/AppKit.h>
#include "SDL.h"

void Mac_I_FatalError(const char* errortext)
{
	// Close window or exit fullscreen and release mouse capture
	SDL_Quit();
	
	const CFStringRef errorString = CFStringCreateWithCStringNoCopy( kCFAllocatorDefault, 
		errortext, kCFStringEncodingASCII, kCFAllocatorNull );
	if ( NULL != errorString )
	{
		CFOptionFlags dummy;
	
		CFUserNotificationDisplayAlert( 0, kCFUserNotificationStopAlertLevel, NULL, NULL, NULL, 
			CFSTR( "Fatal Error" ), errorString, CFSTR( "Exit" ), NULL, NULL, &dummy );
		CFRelease( errorString );
	}
}

// [rc4l] Open a URL in the default browser via NSWorkspace. The scheme is already validated as
// http/https ASCII by the shared I_OpenURL before this is reached; NSURL parsing here is a second
// gate (a string it can't parse yields nil and nothing is opened).
void Mac_I_OpenURL(const char* url)
{
	if ( url == NULL )
		return;
	@autoreleasepool
	{
		NSString* str = [NSString stringWithUTF8String:url];
		NSURL* nsurl = ( str != nil ) ? [NSURL URLWithString:str] : nil;
		if ( nsurl != nil )
			[[NSWorkspace sharedWorkspace] openURL:nsurl];
	}
}

// [rc4l] Synchronous HTTPS GET for the auto-update check, via NSURLSession (system TLS + CA store).
// Runs on the updater's background thread; a semaphore makes it blocking with a hard time cap so a
// slow/hung server can never wedge the thread. Fills `out` with the response body on a 2xx and returns
// true; returns false (out emptied) on timeout, any transport error, or a non-2xx status. `urlStr`
// must be a full https URL. This is fail-safe by design: false simply means "we don't know", so the
// update notice stays hidden.
extern "C" bool Mac_HttpsGet(const char* urlStr, char* out, int outSize, int timeoutSecs)
{
	if ( out == NULL || outSize <= 0 )
		return false;
	out[0] = '\0';
	if ( urlStr == NULL )
		return false;

	@autoreleasepool
	{
		NSString* s = [NSString stringWithUTF8String:urlStr];
		NSURL* url = ( s != nil ) ? [NSURL URLWithString:s] : nil;
		if ( url == nil )
			return false;

		NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:url];
		[req setTimeoutInterval:(NSTimeInterval)timeoutSecs];
		[req setValue:@"ZandroX-updater" forHTTPHeaderField:@"User-Agent"]; // GitHub API requires a UA
		[req setValue:@"application/vnd.github+json" forHTTPHeaderField:@"Accept"];

		__block bool ok = false;
		__block NSData* body = nil;
		dispatch_semaphore_t sem = dispatch_semaphore_create( 0 );
		NSURLSessionDataTask* task = [[NSURLSession sharedSession] dataTaskWithRequest:req
			completionHandler:^(NSData* data, NSURLResponse* resp, NSError* err)
			{
				if ( err == nil && [resp isKindOfClass:[NSHTTPURLResponse class]] )
				{
					NSInteger code = [(NSHTTPURLResponse*)resp statusCode];
					if ( code >= 200 && code < 300 )
					{
						ok = true;
						// [rc4l] This TU is compiled without ARC, so a plain `body = data` does NOT retain:
						// once the handler's autorelease pool drains, `data` is freed and the [body length]
						// below messages a dead object (an intermittent crash -- GlitchTip #26/#127). Retain
						// it so it survives past the handler; released after we copy the bytes out.
						body = [data retain];
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
			NSUInteger len = [body length];
			if ( len >= (NSUInteger)outSize )
				len = (NSUInteger)outSize - 1; // truncate an oversized body; the parser tolerates it
			memcpy( out, [body bytes], len );
			out[len] = '\0';
			result = true;
		}
		[body release]; // balance the retain in the handler (nil-safe)
		return result;
	}
}
