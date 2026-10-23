// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A latency stamp: the first moment something happened that no frame has shown yet, taken once a frame that shows it ends.

#ifndef ZX_LATENCY_COMPUTE_H
#define ZX_LATENCY_COMPUTE_H

namespace zx { namespace latency {

// [rc4l] The first unshown moment, 0 for none; armed when it is ready to be shown (input once a tic carries it, a packet once applied).
struct LatencyStamp
{
	long long since = 0;
	bool armed = false;
};

// [rc4l] Notes the moment at nowNs unless an earlier one is still unshown.
void ComputeStampAt(LatencyStamp &stamp, long long nowNs);

// [rc4l] Marks the stamp ready, so the next frame to end shows it; nothing without a stamp.
void ComputeArm(LatencyStamp &stamp);

// [rc4l] When a frame ends: the stamp's moment into sinceNs and true when it was armed, the stamp cleared then.
bool ComputeTakeShown(LatencyStamp &stamp, long long &sinceNs);

}} // namespace zx::latency

#endif // ZX_LATENCY_COMPUTE_H
