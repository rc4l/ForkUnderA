// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/dev-mode/computation/devmode_compute.h"

#include <string.h>

namespace zx { namespace devmode {

bool ComputeDevModeAllowed(bool isClient, bool svCheats) { return !isClient || svCheats; }

DevModeChange ComputeDevModeChange(bool wasOpen, bool isOpen)
{
	if (wasOpen == isOpen) return kDevModeStay;
	return isOpen ? kDevModeRelease : kDevModeHold;
}

bool ComputeDevModeHolds(const char *value, const char *defaultValue) { return strcmp(value, defaultValue) != 0; }

const char *ComputeDevModeArchivedValue(const char *held, const char *live) { return held != NULL ? held : live; }

}} // namespace zx::devmode
