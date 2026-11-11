// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] dev-mode's public header: the gate on test and debug settings (CVAR_FUA_DEV_MODE) and commands (DEV_MODE_CCMD).

#ifndef ZX_DEVMODE_H
#define ZX_DEVMODE_H

class FBaseCVar;

namespace zx { namespace devmode {

// [rc4l] Holds every dev-mode setting at its default as the gate closes and gives each its player's value back as it opens.
void Refresh();

// [rc4l] Whether a dev-mode setting may change now, printing the refusal when not (FBaseCVar::SetGenericRep).
bool MaySet(const FBaseCVar &var);

// [rc4l] Whether a dev-mode command may run now, printing the refusal when not (FDevModeConsoleCommand::Run).
bool MayRun(const char *name);

// [rc4l] The player's own value of a setting the gate holds at its default, or NULL (C_ArchiveCVars).
const char *HeldValue(const FBaseCVar &var);

}} // namespace zx::devmode

#endif // ZX_DEVMODE_H
