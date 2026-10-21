// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The display's refresh as macOS reports it for the main screen, ProMotion's highest rate included.

#include "features/dynres/dynres.h"

#import <AppKit/AppKit.h>

namespace zx { namespace dynres {

double DisplayRefreshHz()
{
	NSScreen *screen = [NSScreen mainScreen];
	if (screen == nil || ![screen respondsToSelector:@selector(maximumFramesPerSecond)]) return 0.0;
	const NSInteger fps = [screen maximumFramesPerSecond];
	return fps > 0 ? (double)fps : 0.0;
}

}} // namespace zx::dynres
