// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: anim_compute.cpp
//
// Description: The client's animation decisions. See anim_compute.h.
//
//-----------------------------------------------------------------------------

#include "features/net-snapshot/computation/anim_compute.h"

using namespace zx::netsnapshot;

unsigned int zx::netsnapshot::ComputeShadowBucket(int gapTics)
{
	const int gapMS = (int)((double)gapTics * 28.57);
	return (gapMS < 100) ? 0 : (gapMS < 250) ? 1
		: (gapMS < 500) ? 2 : (gapMS < 1000) ? 3 : 4;
}
