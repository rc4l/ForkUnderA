// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wipe/wiperandoms.h"
#include "features/handoff/seam.h"

#include "m_random.h"

namespace zx { namespace wipe {

void DrawMeltRandoms(unsigned char *out, int count)
{
	ZX_SEAM("wipe/randoms", Idle);
	for (int i = 0; i < count; i++) out[i] = (unsigned char)M_Random();
}

}} // namespace zx::wipe
