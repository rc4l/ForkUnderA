#include <CoreFoundation/CoreFoundation.h>
#import <AppKit/AppKit.h>

// [rc4l] Mac_I_FatalError belongs to the SDL backend only.
//
// The Cocoa backend supplies its own (posix/cocoa/i_main.mm, routed through
// FConsoleWindow::ShowFatalError), which is better -- it shows the startup console's accumulated
// log alongside the error rather than just the last line -- and building both is a duplicate
// symbol. But posix/sdl/i_system.cpp calls this one, so deleting it outright broke the
// ZX_COCOA_BACKEND=OFF fallback, which exists precisely so a bisect can put macOS back on SDL.
// Guarded rather than removed.
#ifndef ZX_COCOA_BACKEND

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

#endif // !ZX_COCOA_BACKEND

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

// [rc4l] The updater's HTTPS GET (Mac_HttpsGet) and its memory-management helpers now live in
// i_system_cocoa_net.mm -- a sibling TU with NO SDL dependency, so they can be built into an
// AddressSanitizer unit-test target that guards the use-after-free fix (i_system_cocoa_net_test.mm).
