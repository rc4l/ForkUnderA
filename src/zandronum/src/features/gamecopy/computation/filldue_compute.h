// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] When the render copy is taken again: the game changes only in a tic, which runs even while paused, and on a client when a server packet is applied, so once for each is enough.

#ifndef ZX_FILLDUE_COMPUTE_H
#define ZX_FILLDUE_COMPUTE_H

namespace zx { namespace gamecopy {

// [rc4l] What a fill was taken at: the tic (-1 for none yet this level) and the server packets applied by then.
struct FillStamp
{
	int tic = -1;
	unsigned packets = 0;
};

// [rc4l] Whether a frame takes the copy: none taken yet, or a tic ran or a packet was applied since the last; a client took it every frame, the whole map compared each time.
inline bool ComputeFillDue(const FillStamp &now, const FillStamp &filled)
{
	return filled.tic < 0 || now.tic != filled.tic || now.packets != filled.packets;
}

}} // namespace zx::gamecopy

#endif // ZX_FILLDUE_COMPUTE_H
