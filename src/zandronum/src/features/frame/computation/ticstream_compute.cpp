// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/frame/computation/ticstream_compute.h"

namespace zx { namespace frame {

bool ComputeTicStreamStale(bool live, const TicStreamStamp &built, const TicStreamStamp &now)
{
	return !live || built.fill != now.fill || built.cvarSets != now.cvarSets || built.viewActor != now.viewActor ||
		built.fixedColormap != now.fixedColormap;
}

}} // namespace zx::frame
