// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
//
// [MGOOOOOO] How a centre print or an ACS HUDMSG_LOG message is written to the log file.

#ifndef ZX_MIDMESSAGELOG_COMPUTE_H
#define ZX_MIDMESSAGELOG_COMPUTE_H

#include <string>

namespace zx {

// [MGOOOOOO] The message between two of Zandronum's log bars, as Zandronum writes it (no timestamp).
std::string ComputeMidMessageLogText(const char *message);

} // namespace zx

#endif
