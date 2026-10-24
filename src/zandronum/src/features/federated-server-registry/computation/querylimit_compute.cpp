// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/federated-server-registry/computation/querylimit_compute.h"

namespace zx
{

bool ComputeIgnoreLauncherQuery(int answeredFromAddress, int allowedPerAddress)
{
	return answeredFromAddress >= (allowedPerAddress < 1 ? 1 : allowedPerAddress);
}

} // namespace zx
