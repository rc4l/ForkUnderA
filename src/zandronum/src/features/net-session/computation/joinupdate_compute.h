// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What a joining client's full update has to add so each thing arrives where the server holds it.

#ifndef ZX_JOINUPDATE_COMPUTE_H
#define ZX_JOINUPDATE_COMPUTE_H

#include <cstdint>

namespace zx { namespace netsession {

// [rc4l] Whether a spawn's whole-unit coordinates (a short of each fixed_t's integer part) would put a thing somewhere other than these raw fixed_t coordinates.
bool ComputeSpawnRoundsPosition(int64_t x, int64_t y, int64_t z);

}} // namespace zx::netsession

#endif
