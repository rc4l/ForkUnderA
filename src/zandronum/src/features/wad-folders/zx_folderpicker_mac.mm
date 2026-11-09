// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The folder picker on macOS: NSOpenPanel choosing one folder.

#import <AppKit/AppKit.h>

#include "features/wad-folders/zx_folderpicker.h"

namespace zx { namespace wadfolders {

PickResult PickFolderNative(void *, const char *title, std::string &out)
{
	@autoreleasepool
	{
		NSOpenPanel *panel = [NSOpenPanel openPanel];
		[panel setCanChooseDirectories:YES];
		[panel setCanChooseFiles:NO];
		[panel setAllowsMultipleSelection:NO];
		if (title != NULL)
			[panel setMessage:[NSString stringWithUTF8String:title]];

		if ([panel runModal] != NSModalResponseOK)
			return PickResult::Cancelled;

		NSURL *url = [[panel URLs] firstObject];
		const char *path = (url != nil) ? [[url path] fileSystemRepresentation] : NULL;
		if (path == NULL)
			return PickResult::Cancelled;
		out = path;
		return PickResult::Picked;
	}
}

void *MainWindowHandle()
{
	return NULL;
}

}} // namespace zx::wadfolders
