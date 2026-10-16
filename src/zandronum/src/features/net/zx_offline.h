// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A launch that keeps off the network: no socket, no server registry query, no update check (-offline, which a browser's build always has).

#ifndef ZX_OFFLINE_H
#define ZX_OFFLINE_H

namespace zx { namespace net {

bool OfflineLaunch();

}} // namespace zx::net

#endif // ZX_OFFLINE_H
