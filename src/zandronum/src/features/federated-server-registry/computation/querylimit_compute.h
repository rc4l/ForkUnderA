// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] How many launcher queries a server answers from one address within sv_queryignoretime before it says it is ignoring it.

#ifndef ZX_QUERYLIMIT_COMPUTE_H
#define ZX_QUERYLIMIT_COMPUTE_H

namespace zx
{

// [rc4l] Whether a query is ignored, given how many from its address were answered in the window and how many one address may have (at least one).
bool ComputeIgnoreLauncherQuery(int answeredFromAddress, int allowedPerAddress);

} // namespace zx

#endif // ZX_QUERYLIMIT_COMPUTE_H
