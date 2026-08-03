#include <CoreFoundation/CoreFoundation.h>
#import <AppKit/AppKit.h>

// [rc4l] Mac_I_FatalError used to live here, as an SDL_Quit() plus a CFUserNotification alert.
// Upstream's Cocoa backend supplies its own (posix/cocoa/i_main.mm, routed through
// FConsoleWindow::ShowFatalError), which is better -- it shows the startup console's accumulated
// log alongside the error instead of just the last line -- and keeping both is a duplicate symbol.
// The SDL_Quit() went with it: the Cocoa backend owns teardown, and this file no longer needs SDL.

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
