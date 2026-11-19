// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO

#include "computation/midmessagelog_compute.h"

namespace zx {

std::string ComputeMidMessageLogText(const char *message)
{
	static const char logbar[] = "\n<------------------------------->\n";
	return std::string(logbar) + message + logbar;
}

} // namespace zx
