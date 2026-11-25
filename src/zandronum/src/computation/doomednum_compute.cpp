// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO

#include "computation/doomednum_compute.h"

namespace zx {

DecorateEdNumClaim ComputeDecorateEdNumClaim(bool hasExisting, bool existingNamesClass, int existingSpecial)
{
	if (!hasExisting)
		return DecorateEdNumClaim::Take;
	if (existingSpecial == -2)
		return DecorateEdNumClaim::TakeAndWarnTwice;
	if (!existingNamesClass && existingSpecial > 0)
		return DecorateEdNumClaim::KeepSpecial;
	return DecorateEdNumClaim::Take;
}

} // namespace zx
