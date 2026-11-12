// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Whether a client chats or changes its userinfo too often, behind SERVER_CheckTimeInstancesBufferForFlood.

#ifndef ZX_FLOODCHECK_COMPUTE_H
#define ZX_FLOODCHECK_COMPUTE_H

namespace zx { namespace net {

// [rc4l] How many earlier instances the rule looks back over.
constexpr int TIME_INSTANCES = 3;

// [rc4l] Ported from zandronum@227ae3070e3f9e0dcb1889d75b2b50f728f0fa70: a flood is a second instance within 7 tics, a third within 42 or a fourth within 105, oldestFirst holding the earlier ticks (0 for none yet).
bool ComputeTimeInstancesFlood(const int (&oldestFirst)[TIME_INSTANCES], int gametic);

}} // namespace zx::net

#endif // ZX_FLOODCHECK_COMPUTE_H
