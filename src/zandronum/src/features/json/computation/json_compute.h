// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A string written into JSON, escaped one way for every feature that writes JSON.

#ifndef ZX_JSON_COMPUTE_H
#define ZX_JSON_COMPUTE_H

#include <string>

namespace zx { namespace json {

// [rc4l] What happens to a control character other than newline, carriage return and tab: escaped as \u00XX, or dropped for a reader that refuses those escapes.
enum class JsonControls { Escape, Drop };

// [rc4l] text as the inside of a JSON string: quotes and backslashes escaped, newline, carriage return and tab by name, and other control characters as controls says.
std::string ComputeJsonEscape(const std::string &text, JsonControls controls = JsonControls::Escape);

// [rc4l] text as a JSON string, escaped and quoted.
std::string ComputeJsonQuoted(const std::string &text, JsonControls controls = JsonControls::Escape);

}} // namespace zx::json

#endif // ZX_JSON_COMPUTE_H
