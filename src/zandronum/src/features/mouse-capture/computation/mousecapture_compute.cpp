// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/mouse-capture/computation/mousecapture_compute.h"

namespace zx {

MouseCapture ComputeMouseCapture(bool wantMouse, bool held)
{
	if (!wantMouse) return { false, true, true };
	if (held) return { true, true, false };
	return { false, false, true };
}

} // namespace zx
