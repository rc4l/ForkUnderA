// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The rules behind ACS's GetWadInfo and LumpGetInfo, kept apart from the wad collection they read.

#ifndef ZX_WADINFO_COMPUTE_H
#define ZX_WADINFO_COMPUTE_H

namespace zx { namespace wadinfo {

// [rc4l] Whether an index a script hands in names one of count entries, a negative one never.
bool ComputeIndexInRange(int index, unsigned count);

// [rc4l] Ported from zandronum@57fe0dc1ae24ae1c6a58bc2c2ce0f2a8460344cf: a wad's parent as GetWadInfo gives it, -1 for a wad no other file holds.
int ComputeParentWadInfo(int parentWadNum, int wadNum);

}} // namespace zx::wadinfo

#endif
