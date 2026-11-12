// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] When a client's move takes away the chatting and in-console statuses its CLC_SETSTATUS gave.

#ifndef ZX_STATUSCHANGE_COMPUTE_H
#define ZX_STATUSCHANGE_COMPUTE_H

namespace zx {

// [rc4l] Ported from zandronum@50088e45a0184aa3ea1fe2b530e25389439da87b: a move clears them unless they changed this tic, since CLC_SETSTATUS is read before the move it came with.
bool ComputeMoveClearsStatuses(int lastStatusChangeTick, int gametic);

} // namespace zx

#endif
