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

std::string SymbolName(void *address)
{
	Dl_info info;
	if (dladdr(address, &info) == 0 || info.dli_sname == nullptr) return std::string();
	int status = 0;
	char *demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
	const std::string out = status == 0 && demangled != nullptr ? demangled : info.dli_sname;
	std::free(demangled);
	return out;
}

}} // namespace zx::telemetry
