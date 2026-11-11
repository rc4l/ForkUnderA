// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] The text a fatal error leaves in the logs, so a launch that stops on one says why without its dialog being read.

#ifndef ZX_FATALLOG_COMPUTE_H
#define ZX_FATALLOG_COMPUTE_H

#include <string>

namespace zx { namespace fatallog {

// [Doruk-Aksoy] The marker every logged fatal error starts with, which fuactl looks for.
extern const char *const kFatalLogMarker;

// [Doruk-Aksoy] The marker and the message's first line, its later lines indented under it, CRLF read as LF and trailing blank lines dropped.
std::string ComputeFatalLogText(const char *message);

// [Doruk-Aksoy] Whether this fatal error is the run's first and so is logged, since I_FatalError and the catch it throws to report the same one.
bool ComputeFatalLogClaim(bool &alreadyLogged);

}} // namespace zx::fatallog

#endif // ZX_FATALLOG_COMPUTE_H
