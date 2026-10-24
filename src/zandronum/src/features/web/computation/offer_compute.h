// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which file in the page's home fua_offer downloads for the player, such as a save to send with a report.

#ifndef ZX_OFFER_COMPUTE_H
#define ZX_OFFER_COMPUTE_H

#include <string>

namespace zx { namespace web {

// [rc4l] The file for arg: a path as given, or a save's name in saveDir with .zds added when it has no extension; empty for no arg.
std::string ComputeOfferedPath(const std::string &arg, const std::string &saveDir);

}} // namespace zx::web

#endif // ZX_OFFER_COMPUTE_H
