// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What kind of world pass the backend is drawing, for the per-pass timings a frame's stats print.

#ifndef ZX_PASSNAME_COMPUTE_H
#define ZX_PASSNAME_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] The pass's kind, asked in the order the backend's flags nest: the player's view, a line mirror, a stacked view, a camera texture, the sky portal, else a plane mirror.
const char *ComputeWorldPassName(bool mainView, int lineMirror, int stackSlot, bool cameraPass, bool skyPortal);

}} // namespace zx::hwrender

#endif
