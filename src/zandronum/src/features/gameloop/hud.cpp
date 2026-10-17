// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gameloop/hud.h"
#include "features/handoff/seam.h"
#include "features/draw2d/record.h"
#include "features/frame/framedriver.h"   // [rc4l] GLDrawsFrame

#include "c_cvars.h"

#include <vector>

// [rc4l] Off draws the HUD live every frame, as GL does, so a scenario can hold the recording to it.
CVAR(Bool, fua_hud_record, true, 0)

namespace zx { namespace gameloop {

using namespace zx::handoff;

namespace {

std::vector<zx::draw2d::Quad2D> g_hud;
HudKey g_made;
bool g_valid = false;

} // namespace

void RecordHUD(bool shows, const HudKey &now, void (*draw)())
{
	if (!shows || !fua_hud_record || zx::frame::GLDrawsFrame())
	{
		g_valid = false;
		return;
	}
	if (!ComputeHUDRecordDue(g_valid, g_made, now)) return;
	ZX_SEAM("handoff/hud_record", Handoff);
	zx::draw2d::BeginCapture2D(g_hud);
	draw();
	zx::draw2d::EndCapture2D();
	g_made = now;
	g_valid = true;
}

bool ReplayHUD()
{
	if (!g_valid) return false;
	zx::draw2d::Replay2D(g_hud);
	return true;
}

}} // namespace zx::gameloop
