// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] When dev-mode tools (CVAR_FUA_DEV_MODE, DEV_MODE_CCMD) may run, and what the gate does as that changes.

#ifndef ZX_DEVMODE_COMPUTE_H
#define ZX_DEVMODE_COMPUTE_H

namespace zx { namespace devmode {

// [rc4l] Dev-mode tools run anywhere but on a client whose server disallows cheats.
bool ComputeDevModeAllowed(bool isClient, bool svCheats);

// [rc4l] What the gate does to every dev-mode setting when the answer above changes.
enum DevModeChange { kDevModeStay, kDevModeHold, kDevModeRelease };
DevModeChange ComputeDevModeChange(bool wasOpen, bool isOpen);

// [rc4l] A closing gate holds a setting only when it is off its default, so a setting at its default runs no callback.
bool ComputeDevModeHolds(const char *value, const char *defaultValue);

// [rc4l] The config keeps the player's own value of a held setting, not the default the gate holds it at.
const char *ComputeDevModeArchivedValue(const char *held, const char *live);

}} // namespace zx::devmode

#endif // ZX_DEVMODE_COMPUTE_H
