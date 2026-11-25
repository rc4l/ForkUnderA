// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida
//
// [Kaori-Chani] fua_tictime_report: the shared time base's pacing, printed from the tic loop every
// few seconds and by the fua_tictime command on demand.

#ifndef ZX_TICPACING_H
#define ZX_TICPACING_H

namespace zx { namespace ticpacing {

// [Kaori-Chani] Called once each pass of a tic loop with the engine's own tic count, which is
// what makes this a measure of pacing rather than of the arithmetic.
void Tick(unsigned int gametics);

}} // namespace zx::ticpacing

#endif // ZX_TICPACING_H
