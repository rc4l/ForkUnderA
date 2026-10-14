// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A view drawn through a CAMetalLayer, which the Vulkan backend's swapchain takes on macOS through MoltenVK, with CocoaView's cursor (implemented in i_video.mm).

#ifndef COCOA_METALVIEW_H_INCLUDED
#define COCOA_METALVIEW_H_INCLUDED

#import <Cocoa/Cocoa.h>

@interface CocoaMetalView : NSView
{
	NSCursor* m_cursor;
}

- (void)resetCursorRects;

- (void)setCursor:(NSCursor*)cursor;

@end

#endif // COCOA_METALVIEW_H_INCLUDED
