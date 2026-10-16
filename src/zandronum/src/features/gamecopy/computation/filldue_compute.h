// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] When the render copy is taken again: the game changes only in a tic, which runs even while paused, so once a tic is enough.

#ifndef ZX_FILLDUE_COMPUTE_H
#define ZX_FILLDUE_COMPUTE_H

namespace zx { namespace gamecopy {

// [rc4l] Whether a frame takes the copy: the tic moved on since the last fill (-1 for none yet), or every frame, as a client predicting its player between tics needs.
inline bool ComputeFillDue(int tic, int filledTic, bool everyFrame)
{
	return everyFrame || filledTic < 0 || tic != filledTic;
}

}} // namespace zx::gamecopy

#endif // ZX_FILLDUE_COMPUTE_H
