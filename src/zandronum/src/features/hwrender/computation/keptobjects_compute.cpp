// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/keptobjects_compute.h"

namespace zx { namespace hwrender {

bool ComputeBufferOwnsMemory(bool dynamic, bool staging)
{
	return !dynamic && !staging;
}

}} // namespace zx::hwrender
