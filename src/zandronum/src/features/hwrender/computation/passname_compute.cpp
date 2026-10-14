// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/passname_compute.h"

namespace zx { namespace hwrender {

const char *ComputeWorldPassName(bool mainView, int lineMirror, int stackSlot, bool cameraPass, bool skyPortal)
{
	if (mainView) return "main";
	if (lineMirror >= 0) return "mirror";
	if (stackSlot >= 0) return "stack";
	if (cameraPass) return "camera";
	return skyPortal ? "sky" : "planemirror";
}

}} // namespace zx::hwrender
