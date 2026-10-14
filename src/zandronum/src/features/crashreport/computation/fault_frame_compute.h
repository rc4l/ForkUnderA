// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Where a crash backtrace starts walking: the faulting instruction, or the caller when the fault was a call through a null pointer.

#ifndef ZX_FAULT_FRAME_COMPUTE_H
#define ZX_FAULT_FRAME_COMPUTE_H

#include <stdint.h>

namespace zx {

struct FaultFrame
{
	uint64_t pc;
	uint64_t sp;
};

// [rc4l] A call to address 0 faults before its callee has a frame, so the walk starts from the return address the call pushed (`stackTop`, read at sp) with that slot popped; any other fault starts where it is.
FaultFrame ComputeFaultFrame(uint64_t pc, uint64_t sp, uint64_t stackTop);

} // namespace zx

#endif // ZX_FAULT_FRAME_COMPUTE_H
