// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-lineattack/computation/lineattack_compute.h"

#include <algorithm>

namespace zx { namespace acs {

int ComputeLineAttackShooter(int tid, bool hasActivator)
{
	if (tid != 0) return kLineAttackFromTid;
	return hasActivator ? kLineAttackFromActivator : kLineAttackNothing;
}

bool ComputeFirstWarning(std::vector<int> &warned, int script)
{
	if (std::find(warned.begin(), warned.end(), script) != warned.end()) return false;
	warned.push_back(script);
	return true;
}

}} // namespace zx::acs
