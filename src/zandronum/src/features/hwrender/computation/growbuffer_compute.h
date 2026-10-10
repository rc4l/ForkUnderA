// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The size a grow-only GPU buffer is re-created at, for the per-frame streams (sprites, the 2D layer, sky, stacked and horizon surfaces).

#ifndef ZX_GROWBUFFER_COMPUTE_H
#define ZX_GROWBUFFER_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] The capacity a buffer short of needed elements takes: half again as many plus pad, so a frame that grows a little does not re-create it every time.
unsigned int ComputeGrownCapacity(unsigned int needed, unsigned int pad);

}} // namespace zx::hwrender

#endif // ZX_GROWBUFFER_COMPUTE_H
