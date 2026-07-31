// [rc4l] Declarations for the macOS updater networking seam (i_system_cocoa_net.mm). Mac_RetainBody
// and Mac_CopyBody are the two memory-management steps of Mac_HttpsGet, exposed so a unit test can
// drive them across a real autorelease-pool drain without any network (i_system_cocoa_net_test.mm).
// Objective-C only: the NSData type makes this header ObjC++-exclusive by design.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef I_SYSTEM_COCOA_NET_H
#define I_SYSTEM_COCOA_NET_H

@class NSData;

#ifdef __cplusplus
extern "C" {
#endif

// Retain `data` so it outlives the async completion handler's autorelease pool; balance with a
// release once Mac_CopyBody has copied the bytes out. Dropping this retain reintroduces the
// intermittent first-launch use-after-free (GlitchTip #127 / issue #122).
NSData *Mac_RetainBody(NSData *data);

// Copy up to outSize-1 bytes of `body` into `out`, NUL-terminating, and return the byte count.
// Returns 0 (emptying `out` when it can) for a nil body or a non-positive buffer size.
int Mac_CopyBody(NSData *body, char *out, int outSize);

#ifdef __cplusplus
}
#endif

#endif
