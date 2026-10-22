// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/web/computation/offer_compute.h"

namespace zx { namespace web {

std::string ComputeOfferedPath(const std::string &arg, const std::string &saveDir)
{
	if (arg.empty()) return std::string();
	if (arg.find('/') != std::string::npos) return arg;
	const std::string name = arg.find('.') == std::string::npos ? arg + ".zds" : arg;
	if (saveDir.empty() || saveDir.back() == '/') return saveDir + name;
	return saveDir + "/" + name;
}

}} // namespace zx::web
