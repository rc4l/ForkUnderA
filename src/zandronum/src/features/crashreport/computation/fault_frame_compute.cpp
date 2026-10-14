// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/crashreport/computation/fault_frame_compute.h"

namespace zx {

FaultFrame ComputeFaultFrame(uint64_t pc, uint64_t sp, uint64_t stackTop)
{
	FaultFrame f;
	if (pc == 0)
	{
		f.pc = stackTop;
		f.sp = sp + 8;
	}
	else
	{
		f.pc = pc;
		f.sp = sp;
	}
	return f;
}

} // namespace zx
