/*
 ** i_main.mm
 **
 **---------------------------------------------------------------------------
 ** Copyright 2012-2015 Alexey Lysiuk
 ** All rights reserved.
 **
 ** Redistribution and use in source and binary forms, with or without
 ** modification, are permitted provided that the following conditions
 ** are met:
 **
 ** 1. Redistributions of source code must retain the above copyright
 **    notice, this list of conditions and the following disclaimer.
 ** 2. Redistributions in binary form must reproduce the above copyright
 **    notice, this list of conditions and the following disclaimer in the
 **    documentation and/or other materials provided with the distribution.
 ** 3. The name of the author may not be used to endorse or promote products
 **    derived from this software without specific prior written permission.
 **
 ** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 ** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 ** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 ** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 ** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 ** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 ** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 ** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 ** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 ** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **---------------------------------------------------------------------------
 **
 */

#include "i_common.h"

#include <sys/sysctl.h>
#include <unistd.h>

// Avoid collision between DObject class and Objective-C
#define Class ObjectClass

#include "c_console.h"
#include "c_cvars.h"
#include "cmdlib.h"
#include "d_main.h"
#include "doomerrors.h"
#include "i_system.h"
#include "m_argv.h"
#include "computation/argv_collect_compute.h"
#include "s_sound.h"
#include "st_console.h"
#include "version.h"
#include "features/crashreport/zx_crashreport.h"   // [rc4l] ZX_CrashReportInit
#include "za_misc.h"                                 // [SB] ZA_PrintVersion
#include "network.h"                                 // [EP] NETWORK_GetState

#undef Class


#define ZD_UNUSED(VARIABLE) ((void)(VARIABLE))


// ---------------------------------------------------------------------------


EXTERN_CVAR(Int,  vid_defwidth )
EXTERN_CVAR(Int,  vid_defheight)
EXTERN_CVAR(Bool, vid_vsync    )
EXTERN_CVAR(Bool, fullscreen   )


// ---------------------------------------------------------------------------

namespace
{

// The maximum number of functions that can be registered with atterm.
const size_t MAX_TERMS = 64;

void      (*TermFuncs[MAX_TERMS])();
const char *TermNames[MAX_TERMS];
size_t      NumTerms;

} // unnamed namespace

// Expose this for i_main_except.cpp
void call_terms()
{
	while (NumTerms > 0)
	{
		TermFuncs[--NumTerms]();
	}
}


void addterm(void (*func)(), const char *name)
{
	// Make sure this function wasn't already registered.

	for (size_t i = 0; i < NumTerms; ++i)
	{
		if (TermFuncs[i] == func)
		{
			return;
		}
	}

	if (NumTerms == MAX_TERMS)
	{
		func();
		I_FatalError("Too many exit functions registered.");
	}

	TermNames[NumTerms] = name;
	TermFuncs[NumTerms] = func;

	++NumTerms;
}

void popterm()
{
	if (NumTerms)
	{
		--NumTerms;
	}
}


void Mac_I_FatalError(const char* const message)
{
	I_SetMainWindowVisible(false);

	// [rc4l] A -host server child has no console window; its fatal errors go to stderr, which the
	// hosting parent captures over the pipe -- a window (even if one could be made) has no user.
	if (FConsoleWindow::InstanceExists())
	{
		FConsoleWindow::GetInstance().ShowFatalError(message);
	}
	else
	{
		fprintf(stderr, "%s\n", message);
	}
}


DArgs* Args; // command line arguments


// Newer versions of GCC than 4.2 have a bug with C++ exceptions in Objective-C++ code.
// To work around we'll implement the try and catch in standard C++.
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=61759
void OriginalMainExcept(int argc, char** argv);
void OriginalMainTry(int argc, char** argv)
{
	Args = new DArgs(argc, argv);

	/*
	 killough 1/98:

	 This fixes some problems with exit handling
	 during abnormal situations.

	 The old code called I_Quit() to end program,
	 while now I_Quit() is installed as an exit
	 handler and exit() is called to exit, either
	 normally or abnormally. Seg faults are caught
	 and the error handler is used, to prevent
	 being left in graphics mode or having very
	 loud SFX noise because the sound card is
	 left in an unstable state.
	 */

	atexit(call_terms);
	atterm(I_Quit);

	NSString* exePath = [[NSBundle mainBundle] executablePath];
	progdir = [[exePath stringByDeletingLastPathComponent] UTF8String];
	progdir += "/";

	C_InitConsole(80 * 8, 25 * 8, false);
	D_DoomMain();
}

namespace
{

// [rc4l] The command line, as something that grows.
//
// This was a fixed 64-entry array that main() wrote WITHOUT a bounds check. Sixty-four is plenty for
// a player double-clicking the app and nowhere near enough for a server we start ourselves: the HOST
// tab hands the child one argument per gameplay cvar, two per map in the rotation and two per WAD,
// which reaches a hundred and fifty for an ordinary co-op preset. Every argument past the sixty-
// fourth was stored PAST THE END of the array, over whichever file's statics the linker had placed
// next -- and the server then died seconds later somewhere with no connection to the real fault
// (inside the automap's arrays, inside the object allocator, inside CoreFoundation), differently
// each time and only sometimes at all. The file-open handler below always bounds-checked; main()
// never did.
//
// Fixed by removing the cap rather than raising it. A number picked in advance is the bug: a map
// rotation is as long as the operator wants it, so any constant is one rotation away from being
// wrong again, and silently dropping the arguments past it would lose maps and cvars instead.
// s_argvStorage owns the bytes, s_argv holds the pointers into them, and both grow.
TArray<FString> s_argvStorage;
TArray<char *>  s_argv;

// Keep the two in step: every argument is stored, then pointed at. Growing s_argvStorage moves the
// FString objects but not the characters they own, so pointers handed out earlier stay good.
void AddArg(const char* argument)
{
	s_argvStorage.Push(argument);
	s_argv.Push(s_argvStorage.Last().LockBuffer());
}

// argv for the engine. NULL only if nothing was collected at all, which cannot happen -- argv[0] is
// always there -- but taking &s_argv[0] of an empty array to find that out would be the same class
// of mistake this comment is about.
char** ArgVector()
{
	return (s_argv.Size() > 0) ? &s_argv[0] : NULL;
}

bool s_restartedFromWADPicker;


void NewFailure()
{
	I_FatalError("Failed to allocate memory from system heap");
}

int OriginalMain(int argc, char** argv)
{
	printf(GAMENAME" %s - %s - Cocoa version\nCompiled on %s\n\n",
		GetVersionString(), GetGitTime(), __DATE__);

	seteuid(getuid());
	std::set_new_handler(NewFailure);

	// Set LC_NUMERIC environment variable in case some library decides to
	// clear the setlocale call at least this will be correct.
	// Note that the LANG environment variable is overridden by LC_*
	setenv("LC_NUMERIC", "C", 1);
	setlocale(LC_ALL, "C");

	// Set reasonable default values for video settings

	const NSSize screenSize = [[NSScreen mainScreen] frame].size;
	vid_defwidth  = static_cast<int>(screenSize.width);
	vid_defheight = static_cast<int>(screenSize.height);
	vid_vsync     = true;
	fullscreen    = true;

	OriginalMainExcept(argc, argv);

	return 0;
}

} // unnamed namespace


// ---------------------------------------------------------------------------


@interface ApplicationController : NSResponder
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1060
	<NSApplicationDelegate>
#endif
{
}

- (void)keyDown:(NSEvent*)theEvent;
- (void)keyUp:(NSEvent*)theEvent;

- (void)applicationDidBecomeActive:(NSNotification*)aNotification;
- (void)applicationWillResignActive:(NSNotification*)aNotification;

- (void)applicationDidFinishLaunching:(NSNotification*)aNotification;

- (BOOL)application:(NSApplication*)theApplication openFile:(NSString*)filename;

- (void)processEvents:(NSTimer*)timer;

@end


ApplicationController* appCtrl;


@implementation ApplicationController

- (void)keyDown:(NSEvent*)theEvent
{
	// Empty but present to avoid playing of 'beep' alert sound
	
	ZD_UNUSED(theEvent);
}

- (void)keyUp:(NSEvent*)theEvent
{
	// Empty but present to avoid playing of 'beep' alert sound
	
	ZD_UNUSED(theEvent);
}


- (void)applicationDidBecomeActive:(NSNotification*)aNotification
{
	ZD_UNUSED(aNotification);

	// [rc4l] Unconditional: S_SetSoundPaused answers i_pauseinbackground and i_soundinbackground
	// itself now. Ported from uzdoom@12ed24d066a819a128a54e2359fd0e2d48f641fe.
	S_SetSoundPaused(1);
}

- (void)applicationWillResignActive:(NSNotification*)aNotification
{
	ZD_UNUSED(aNotification);

	// [rc4l] See applicationDidBecomeActive:, this is the half users actually notice.
	S_SetSoundPaused(0);
}


- (void)applicationDidFinishLaunching:(NSNotification*)aNotification
{
	// When starting from command line with real executable path, e.g. ZDoom.app/Contents/MacOS/ZDoom
	// application remains deactivated for an unknown reason.
	// The following call resolves this issue
	[NSApp activateIgnoringOtherApps:YES];

	// Setup timer for custom event loop

	NSTimer* timer = [NSTimer timerWithTimeInterval:0
											 target:self
										   selector:@selector(processEvents:)
										   userInfo:nil
											repeats:YES];
	[[NSRunLoop currentRunLoop] addTimer:timer
								 forMode:NSDefaultRunLoopMode];

	FConsoleWindow::CreateInstance();
	atterm(FConsoleWindow::DeleteInstance);

	exit(OriginalMain(s_argv.Size(), ArgVector()));
}


- (BOOL)application:(NSApplication*)theApplication openFile:(NSString*)filename
{
	ZD_UNUSED(theApplication);

	if (s_restartedFromWADPicker
		|| 0 == [filename length])
	{
		return FALSE;
	}

	// Some parameters from command line are passed to this function
	// These parameters need to be skipped to avoid duplication
	// Note: SDL has different approach to fix this issue, see the same method in SDLMain.m

	const char* const charFileName = [filename UTF8String];

	for (unsigned int i = 0; i < s_argv.Size(); ++i)
	{
		if (0 == strcmp(s_argv[i], charFileName))
		{
			return FALSE;
		}
	}

	AddArg("-file");
	AddArg([filename UTF8String]);

	return TRUE;
}


- (void)processEvents:(NSTimer*)timer
{
	ZD_UNUSED(timer);

	NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

    while (true)
    {
        NSEvent* event = [NSApp nextEventMatchingMask:NSAnyEventMask
											untilDate:[NSDate dateWithTimeIntervalSinceNow:0]
											   inMode:NSDefaultRunLoopMode
											  dequeue:YES];
        if (nil == event)
        {
            break;
        }

		I_ProcessEvent(event);

		[NSApp sendEvent:event];
	}
    
    [NSApp updateWindows];

	[pool release];
}

@end


// ---------------------------------------------------------------------------


namespace
{

NSMenuItem* CreateApplicationMenu()
{
	NSMenu* menu = [NSMenu new];

	[menu addItemWithTitle:[@"About " stringByAppendingString:@GAMENAME]
					   action:@selector(orderFrontStandardAboutPanel:)
				keyEquivalent:@""];
	[menu addItem:[NSMenuItem separatorItem]];
	[menu addItemWithTitle:[@"Hide " stringByAppendingString:@GAMENAME]
					   action:@selector(hide:)
				keyEquivalent:@"h"];
	[[menu addItemWithTitle:@"Hide Others"
						action:@selector(hideOtherApplications:)
				 keyEquivalent:@"h"]
	 setKeyEquivalentModifierMask:NSAlternateKeyMask | NSCommandKeyMask];
	[menu addItemWithTitle:@"Show All"
					   action:@selector(unhideAllApplications:)
				keyEquivalent:@""];
	[menu addItem:[NSMenuItem separatorItem]];
	[menu addItemWithTitle:[@"Quit " stringByAppendingString:@GAMENAME]
					   action:@selector(terminate:)
				keyEquivalent:@"q"];

	NSMenuItem* menuItem = [NSMenuItem new];
	[menuItem setSubmenu:menu];

	if ([NSApp respondsToSelector:@selector(setAppleMenu:)])
	{
		[NSApp performSelector:@selector(setAppleMenu:) withObject:menu];
	}

	return menuItem;
}

NSMenuItem* CreateEditMenu()
{
	NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Edit"];

	[menu addItemWithTitle:@"Undo"
						action:@selector(undo:)
				 keyEquivalent:@"z"];
	[menu addItemWithTitle:@"Redo"
						action:@selector(redo:)
				 keyEquivalent:@"Z"];
	[menu addItem:[NSMenuItem separatorItem]];
	[menu addItemWithTitle:@"Cut"
						action:@selector(cut:)
				 keyEquivalent:@"x"];
	[menu addItemWithTitle:@"Copy"
						action:@selector(copy:)
				 keyEquivalent:@"c"];
	[menu addItemWithTitle:@"Paste"
						action:@selector(paste:)
				 keyEquivalent:@"v"];
	[menu addItemWithTitle:@"Delete"
						action:@selector(delete:)
				 keyEquivalent:@""];
	[menu addItemWithTitle:@"Select All"
						action:@selector(selectAll:)
				 keyEquivalent:@"a"];

	NSMenuItem* menuItem = [NSMenuItem new];
	[menuItem setSubmenu:menu];

	return menuItem;
}

NSMenuItem* CreateWindowMenu()
{
	NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Window"];
	[NSApp setWindowsMenu:menu];

	[menu addItemWithTitle:@"Minimize"
					action:@selector(performMiniaturize:)
			 keyEquivalent:@"m"];
	[menu addItemWithTitle:@"Zoom"
					action:@selector(performZoom:)
			 keyEquivalent:@""];
	[menu addItem:[NSMenuItem separatorItem]];
	[menu addItemWithTitle:@"Bring All to Front"
					action:@selector(arrangeInFront:)
			 keyEquivalent:@""];

	NSMenuItem* menuItem = [NSMenuItem new];
	[menuItem setSubmenu:menu];

	return menuItem;
}

void CreateMenu()
{
	NSMenu* menuBar = [NSMenu new];
	[menuBar addItem:CreateApplicationMenu()];
	[menuBar addItem:CreateEditMenu()];
	[menuBar addItem:CreateWindowMenu()];

	[NSApp setMainMenu:menuBar];
}

void ReleaseApplicationController()
{
	if (NULL != appCtrl)
	{
		[NSApp setDelegate:nil];
		[NSApp deactivate];

		[appCtrl release];
		appCtrl = NULL;
	}
}

} // unnamed namespace


int main(int argc, char** argv)
{
	// [rc4l] The counting, the bound and which arguments are dropped live in ComputeCollectArgv,
	// where they are tested; getting them wrong here is what corrupted a hosted server's memory.
	// What is left is copying into storage the engine owns.
	const zx::CollectedArgv collected = zx::ComputeCollectArgv(argc, argv);

	s_restartedFromWADPicker = collected.bRestartedFromWadPicker;

	for (size_t i = 0; i < collected.args.size(); ++i)
	{
		AddArg(collected.args[i].c_str());
	}

	// [rc4l][SB][BB] Zandronum's entry-point work, before any Cocoa object exists.
	//
	// --version must print and exit without side effects, so ZA_PrintVersion() runs first and
	// crash reporting starts only after it has declined to bail.
	Args = new DArgs(s_argv.Size(), ArgVector());

	if (ZA_PrintVersion())
	{
		return 0;
	}

	ZX_CrashReportInit();

	printf(GAMENAME" %s - %s - Cocoa version\nCompiled on %s\n",
		GetVersionString(), GetGitTime(), __DATE__);

	// [rc4l][BB] A dedicated server must never open an NSApplication run loop -- it has no window,
	// no menu bar and no user, and [NSApp run] would simply never return. Hand straight to the
	// engine instead. This is the Cocoa equivalent of the -host branch that posix/sdl/i_main.cpp
	// uses to skip SDL_INIT_VIDEO.
#ifdef SERVER_ONLY
	Args->AppendArg( "-host" );
#endif
	if ( Args->CheckParm( "-host" ) )
	{
		OriginalMainExcept(s_argv.Size(), ArgVector());
		return EXIT_SUCCESS;
	}

	NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

	[NSApplication sharedApplication];

	// The following code isn't mandatory,
	// but it enables to run the application without a bundle
	if ([NSApp respondsToSelector:@selector(setActivationPolicy:)])
	{
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
	}

	CreateMenu();

	atterm(ReleaseApplicationController);

	appCtrl = [ApplicationController new];
	[NSApp setDelegate:appCtrl];
	[NSApp run];

	[pool release];

	return EXIT_SUCCESS;
}
