// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] How many bytes a clientside demo's record takes, which its buffer must have room for before it is written.

#ifndef ZX_DEMOBYTES_COMPUTE_H
#define ZX_DEMOBYTES_COMPUTE_H

#include <cstddef>
#include <initializer_list>

namespace zx {

// [rc4l] Adapted from zandronum@25198379d37173b00002fc648c442f351a85edec: the fixed-width fields' bytes and each string's characters with its null terminator, which upstream counts by hand at each record.
size_t ComputeDemoBytes(size_t fixedBytes, std::initializer_list<size_t> stringLengths);

} // namespace zx

#endif
