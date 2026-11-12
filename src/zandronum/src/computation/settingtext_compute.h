// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The text a server prints for a float setting's new value.

#ifndef ZX_SETTINGTEXT_COMPUTE_H
#define ZX_SETTINGTEXT_COMPUTE_H

#include <string>

namespace zx {

// [rc4l] Adapted from zandronum@ad1ae23c5589ca4a516a8219a18326000284854a and zandronum@f7888a42ab357e45bcd4c21bf1e6406fdfa8ddbc: infinity and NaN by name, else the value to maxDecimals places, or printf's six when it is 0 or less.
std::string ComputeFloatSettingText(float value, int maxDecimals);

} // namespace zx

#endif
