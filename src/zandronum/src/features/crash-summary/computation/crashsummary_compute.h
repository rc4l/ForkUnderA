// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_CRASHSUMMARY_COMPUTE_H
#define ZX_CRASHSUMMARY_COMPUTE_H

#include "features/telemetry/computation/telemetry_compute.h"
#include <cstdint>
#include <string>

namespace zx { namespace crashsummary {

// [Doruk-Aksoy] The last minute a crash report sends, kept in every build: each second's frame time mean and max, the frames over kStallMs, and the process's memory.
class ComputeSummaryRing
{
public:
	static const int kSeconds = 60;
	static constexpr double kStallMs = 50.0;
	ComputeSummaryRing();
	// [Doruk-Aksoy] A frame that took ms ended at nowNs; true when a second has passed since the last one closed, and the caller then closes it.
	bool NoteFrame(double ms, int64_t nowNs);
	// [Doruk-Aksoy] Closes the second with the memory read at its end, negative where a platform cannot read it.
	void CloseSecond(double privateBytes, double gpuBytes);
	// [Doruk-Aksoy] The summary in telemetry's flight-summary lines, over the last 30 seconds.
	std::string Text() const;
	int Seconds() const { return m_seconds.Rows(); }

private:
	zx::telemetry::FrameRing m_seconds;
	int64_t m_secondStartNs, m_closed;
	double m_sum, m_max;
	int m_frames, m_stalls[kSeconds], m_stallHead;
};

}} // namespace zx::crashsummary

#endif
