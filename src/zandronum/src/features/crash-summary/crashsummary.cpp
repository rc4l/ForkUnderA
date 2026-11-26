// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/crash-summary/crashsummary.h"
#include "features/crash-summary/computation/crashsummary_compute.h"
#include "features/telemetry/osprobe.h"
#include "features/telemetry/telemetry.h"
#include <cstdint>
#include <string>

namespace zx { namespace crashsummary {

namespace {

ComputeSummaryRing g_ring;
int64_t g_lastNs = 0;
void (*g_listener)(const char *) = nullptr;

} // namespace

void NoteFrame()
{
	const int64_t now = zx::telemetry::NowNs();
	if (g_lastNs != 0 && g_ring.NoteFrame((now - g_lastNs) / 1e6, now))
	{
		// [Doruk-Aksoy] The OS probe's light readings once a second, never its heap walk.
		zx::telemetry::OsReadings os;
		zx::telemetry::ReadOs(os, false);
		g_ring.CloseSecond(os.privateBytes, os.gpuProcessBytes);
		if (g_listener != nullptr) g_listener(g_ring.Text().c_str());
	}
	g_lastNs = now;
}

void SetListener(void (*listen)(const char *summary))
{
	g_listener = listen;
}

}} // namespace zx::crashsummary
