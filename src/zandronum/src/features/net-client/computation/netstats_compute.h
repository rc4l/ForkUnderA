// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida
//
// [KI] The one percentage formula behind stat nettraffic's compression factor and zstd share, extracted so both getters use a single tested decision.

#ifndef ZX_NETSTATS_COMPUTE_H
#define ZX_NETSTATS_COMPUTE_H

namespace zx { namespace netclient {

// [KI] part as a percentage of whole, 100 when they are equal including 0 of 0, and 100 for a whole of 0 outright since the trackers never report a part without its whole.
float ComputeSharePercent(unsigned int part, unsigned int whole);

}} // namespace zx::netclient

#endif // ZX_NETSTATS_COMPUTE_H
