// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_TIMEDEMO_COMPUTE_H
#define ZX_TIMEDEMO_COMPUTE_H

#include <string>

// [Doruk-Aksoy] A finished -timedemo's line: the tics played, the real tics they took and the frame rate that gives at ticrate.
std::string ComputeTimedemoLine(int gametics, int realtics, int ticrate);

#endif
