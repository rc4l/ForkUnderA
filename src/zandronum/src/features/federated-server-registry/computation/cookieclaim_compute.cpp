// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/federated-server-registry/computation/cookieclaim_compute.h"

namespace zx
{

CookieClaim DecideCookieClaim(bool found, CookiePurpose purpose)
{
	// [rc4l] Destroying on a failed claim would let anyone cancel a stranger's cookie by guessing.
	if (!found)
		return CookieClaim(false, false);

	return CookieClaim(true, purpose == CookiePurpose::ReachProbe);
}

} // namespace zx
