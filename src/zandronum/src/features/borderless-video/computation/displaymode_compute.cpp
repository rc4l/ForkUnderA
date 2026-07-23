// [rc4l] See displaymode_compute.h. Pure; no engine/SDL/Win32 dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "displaymode_compute.h"

namespace zx
{

WindowKind WindowKindForFullscreen(bool fullscreen)
{
	return fullscreen ? WINDOW_BORDERLESS_DESKTOP : WINDOW_NORMAL;
}

void BorderlessWindowRect(int displayX, int displayY, int displayW, int displayH,
                          int *outX, int *outY, int *outW, int *outH)
{
	// Cover the whole display. Kept as its own function so future work-area / multi-monitor
	// adjustments are a one-line change here with a test, not a Win32-glue edit.
	if (outX) *outX = displayX;
	if (outY) *outY = displayY;
	if (outW) *outW = displayW;
	if (outH) *outH = displayH;
}

} // namespace zx
