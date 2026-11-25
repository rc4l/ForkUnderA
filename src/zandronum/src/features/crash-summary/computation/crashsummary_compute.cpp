// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/crash-summary/computation/crashsummary_compute.h"
#include <algorithm>
#include <vector>

namespace zx { namespace crashsummary {

ComputeSummaryRing::ComputeSummaryRing()
	: m_seconds(kSeconds, 6), m_secondStartNs(0), m_closed(0), m_sum(0.0), m_max(0.0), m_frames(0), m_stallHead(0)
{
	std::fill(m_stalls, m_stalls + kSeconds, 0);
}

bool ComputeSummaryRing::NoteFrame(double ms, int64_t nowNs)
{
	if (m_secondStartNs == 0) m_secondStartNs = nowNs;
	m_sum += ms;
	m_max = m_frames == 0 ? ms : std::max(m_max, ms);
	m_frames++;
	if (ms > kStallMs) m_stalls[m_stallHead]++;
	if (nowNs - m_secondStartNs < 1000000000LL) return false;
	m_secondStartNs = nowNs;
	return true;
}

void ComputeSummaryRing::CloseSecond(double privateBytes, double gpuBytes)
{
	const float row[6] = { (float)(m_frames > 0 ? m_sum / m_frames : 0.0), (float)m_max, (float)privateBytes, (float)privateBytes, (float)gpuBytes, (float)gpuBytes };
	m_seconds.Push(m_closed++, row, 6);
	m_sum = m_max = 0.0;
	m_frames = 0;
	m_stallHead = (m_stallHead + 1) % kSeconds;
	m_stalls[m_stallHead] = 0;
}

std::string ComputeSummaryRing::Text() const
{
	std::vector<zx::telemetry::SummaryGauge> gauges;
	if (m_seconds.Rows() > 0 && m_seconds.Row(0)[2] >= 0.0f) gauges.push_back(zx::telemetry::SummaryGauge{ "mem/private_bytes", "bytes", 1 });
	if (m_seconds.Rows() > 0 && m_seconds.Row(0)[4] >= 0.0f) gauges.push_back(zx::telemetry::SummaryGauge{ "gpu/process_bytes", "bytes", 2 });
	int stalls = 0;
	for (int i = 0; i < kSeconds; i++) stalls += m_stalls[i];
	return zx::telemetry::ComputeFlightSummary(m_seconds, 0, gauges, 30, stalls);
}

}} // namespace zx::crashsummary
