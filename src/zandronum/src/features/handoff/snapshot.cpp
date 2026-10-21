// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/handoff/snapshot.h"
#include "features/handoff/seam.h"

#include "v_video.h"

namespace zx { namespace handoff {

namespace {

int g_width = 0, g_height = 0;

} // namespace

void TakeSnapshot()
{
	ZX_SEAM("handoff/snapshot", Handoff);
	g_width = screen != NULL ? SCREENWIDTH : 0;
	g_height = screen != NULL ? SCREENHEIGHT : 0;
}

// [rc4l] ACS run outside a tic before the first snapshot, as a level's start can, reads the screen on the main thread.
int TicScreenWidth()
{
	return g_width == 0 && screen != NULL && OnMainThread() ? SCREENWIDTH : g_width;
}

int TicScreenHeight()
{
	return g_height == 0 && screen != NULL && OnMainThread() ? SCREENHEIGHT : g_height;
}

}} // namespace zx::handoff
