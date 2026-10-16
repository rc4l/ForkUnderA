// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Stacks on Linux and macOS for fua_allocsites: the C library's backtrace walks them, and dladdr names a frame from the dynamic symbol table, demangled.

#include "features/telemetry/osprobe.h"

#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <cstdlib>

namespace zx { namespace telemetry {

int CaptureStack(void **frames, int max)
{
	return backtrace(frames, max);
}

std::vector<std::string> SymbolNames(const std::vector<void *> &addresses)
{
	std::vector<std::string> names;
	for (void *address : addresses)
	{
		Dl_info info;
		if (dladdr(address, &info) == 0 || info.dli_sname == nullptr) { names.push_back(std::string()); continue; }
		int status = 0;
		char *demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
		names.push_back(status == 0 && demangled != nullptr ? demangled : info.dli_sname);
		std::free(demangled);
	}
	return names;
}

}} // namespace zx::telemetry
