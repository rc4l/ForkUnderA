// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_CHEATAMOUNT_COMPUTE_H
#define ZX_CHEATAMOUNT_COMPUTE_H

namespace zx { namespace cheatamount {

// [Doruk-Aksoy] The first demo version whose DEM_GIVECHEAT/DEM_TAKECHEAT carry a 4-byte amount instead of a 2-byte one.
const int kLongAmountDemoVersion = 0x21C;

// [Doruk-Aksoy] The amount give/take sends for its console argument: 0 without one, else the number clamped to [1, INT_MAX].
int ComputeCheatAmountArg(const char *arg);

// [Doruk-Aksoy] The amount the receiver applies for one read off a stream, with a negative (hostile or truncated) value treated as none given.
int ComputeCheatAmountFromWire(int raw);

// [Doruk-Aksoy] How many bytes a DEM_GIVECHEAT/DEM_TAKECHEAT amount takes: 2 in an older demo being played back, else 4.
int ComputeCheatAmountWireBytes(bool demoPlayback, int demoVersion);

// [Doruk-Aksoy] a + b, held at INT_MIN/INT_MAX instead of overflowing.
int ComputeSaturatingAdd(int a, int b);

// [Doruk-Aksoy] a * b, held at INT_MIN/INT_MAX instead of overflowing.
int ComputeSaturatingMul(int a, int b);

}} // namespace zx::cheatamount

#endif // ZX_CHEATAMOUNT_COMPUTE_H
