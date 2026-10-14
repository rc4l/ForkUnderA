// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The backend's window on macOS: the engine window's Metal-layered view in a launch without GL, else a window beside it, drawn through MoltenVK, and the Vulkan loader the .app carries.

#ifdef FUA_DILIGENT

#import <Cocoa/Cocoa.h>

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#import "posix/cocoa/metalview.h"   // [rc4l] CocoaMetalView, which the engine's own window uses too
#include "features/hwrender/diligent/dgplatform.h"
#include "features/hwrender/backendonly.h"   // [rc4l] BackendOnly

// [rc4l] The engine window's content view (posix/cocoa/i_video.mm), nil before there is one.
void *I_EngineContentView();

namespace zx { namespace hwrender {

void *Fua_CreateBackendWindow(const char *title, int w, int h)
{
	// [rc4l] A launch without GL gave the engine's window a Metal-layered view, which the swapchain takes over whole.
	if (BackendOnly() && I_EngineContentView() != NULL) return I_EngineContentView();
	// [rc4l] Beside GL a window of its own, since nothing here nests one view's swapchain inside GL's.
	const NSRect rect = NSMakeRect(0, 0, w, h);
	NSWindow *window = [[NSWindow alloc] initWithContentRect:rect
		styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable)
		backing:NSBackingStoreBuffered
		defer:NO];
	if (window == nil) return NULL;
	[window setReleasedWhenClosed:NO];
	CocoaMetalView *view = [[CocoaMetalView alloc] initWithFrame:rect];
	[view setWantsLayer:YES];
	[window setContentView:view];
	[view release];
	[window setTitle:[NSString stringWithUTF8String:title]];
	[window orderFront:nil];
	return (void *)view;
}

bool Fua_BackendWindowHandles(void *window, BackendWindowHandles &out)
{
	memset(&out, 0, sizeof(out));
	out.nsView = window;
	return window != NULL;
}

// [rc4l] Nothing is nested in the engine's window here, so nothing follows it.
void Fua_SyncBackendWindowToParent(void *) {}

void Fua_ShowBackendWindow(void *window, int visible)
{
	if (window == NULL || window == I_EngineContentView()) return;
	NSWindow *w = [(NSView *)window window];
	if (visible) [w orderFront:nil];
	else [w orderOut:nil];
}

// [rc4l] The application's run loop services every window, and the engine runs it.
void Fua_PumpBackendWindow(void *) {}

bool Fua_RestoreEngineWindow()
{
	NSView *view = (NSView *)I_EngineContentView();
	NSWindow *w = view != nil ? [view window] : nil;
	if (w == nil || ![w isMiniaturized]) return false;
	[w deminiaturize:nil];
	return true;
}

void Fua_PrepareVulkanLoader()
{
	// [rc4l] The .app carries the Vulkan loader and MoltenVK beside the executable and MoltenVK's driver file in its resources (mac_compile.sh); a Vulkan SDK on the machine is used when there is none.
	NSBundle *bundle = [NSBundle mainBundle];
	NSFileManager *files = [NSFileManager defaultManager];
	NSString *icd = [[bundle resourcePath] stringByAppendingPathComponent:@"vulkan/icd.d/MoltenVK_icd.json"];
	if ([files fileExistsAtPath:icd] && getenv("VK_ICD_FILENAMES") == NULL && getenv("VK_DRIVER_FILES") == NULL)
		setenv("VK_DRIVER_FILES", [icd fileSystemRepresentation], 0);
	// [rc4l] Loaded by its path first, so volk's later dlopen by bare name finds it already in the process.
	NSString *loader = [[[bundle executablePath] stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"libvulkan.1.dylib"];
	if ([files fileExistsAtPath:loader]) dlopen([loader fileSystemRepresentation], RTLD_NOW | RTLD_GLOBAL);
}

}} // namespace zx::hwrender

#endif // FUA_DILIGENT
