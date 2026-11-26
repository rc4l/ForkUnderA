// [MGOOOOOO] See ticboxes_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#include "features/hitboxviz/computation/ticboxes_compute.h"

namespace zx { namespace hitboxviz {

bool ComputeTicBoxWanted(int spawnTic, int nowTic, bool missile, bool solid, bool shootable, bool noInteraction)
{
	return spawnTic == nowTic && !noInteraction && (missile || solid || shootable);
}

int ComputeTicBoxExpiry(int madeTime)
{
	return madeTime + 2;
}

bool ComputeTicBoxShown(int expiryTic, int copyTime)
{
	return expiryTic == copyTime + 1;
}

}} // namespace zx::hitboxviz
