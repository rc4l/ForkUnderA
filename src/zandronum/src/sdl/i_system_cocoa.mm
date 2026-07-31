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
