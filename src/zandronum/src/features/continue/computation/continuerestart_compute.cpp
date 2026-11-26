// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/continue/computation/continuerestart_compute.h"

namespace zx
{

bool ComputeAfterRestartDue(bool armed, int armedAtRestart, int restartsNow, bool startingUp)
{
	return armed && restartsNow > armedAtRestart && !startingUp;
}

} // namespace zx
