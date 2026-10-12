// [MobiusU9B] The text of the popup that reports whether writeini saved the config, kept engine-free so it is unit-tested.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#ifndef ZX_SAVESETTINGS_COMPUTE_H
#define ZX_SAVESETTINGS_COMPUTE_H

#include <string>

namespace zx {

// [MobiusU9B] "Settings saved." on success, otherwise the reason, falling back to "unknown error" when there is none.
std::string ComputeSaveSettingsMessage(bool saved, const char *error);

} // namespace zx

#endif
