// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/skulltagscore_compute.h"

namespace zx {

bool ComputeScoresSkulltagPoint(bool requireSkullToScore, bool ownSkullTaken)
{
	return !requireSkullToScore || !ownSkullTaken;
}

} // namespace zx
