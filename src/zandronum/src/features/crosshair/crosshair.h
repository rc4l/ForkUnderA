// [MobiusU9B] Crosshair Options: the size and offset sliders on the stock crosshair, and the target info and telemeter drawn beside it.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#ifndef ZX_CROSSHAIR_H
#define ZX_CROSSHAIR_H

#include "m_fixed.h"
#include "v_font.h"
#include "zstring.h"
#include "features/crosshair/computation/crosshair_compute.h"
#include "features/crosshair/computation/targetinfo_compute.h"
#include "features/crosshair/computation/viewsweep_compute.h"

class AActor;
class FTexture;
struct FTraceResults;

namespace zx { namespace crosshair {

// [MobiusU9B] Zandronum's identify text for the actor in the crosshair, split into its name line and the lines under it.
struct TargetText
{
	FString name;
	FString details;
	EColorRange color;
};

// [MobiusU9B] Where image is drawn this frame in real pixels, from the stock cvars, the sliders, growSize (CrosshairSize) and the 3D view; no image gives a zero size.
CrosshairRect GetRect(FTexture *image, fixed_t growSize);

// [MobiusU9B] The Target info toggles as currently set.
IdentifyToggles ReadIdentifyToggles();

// [MobiusU9B] Whether cl_fua_identify_position puts the identify text beside the crosshair instead of at the bottom of the screen.
bool IsTargetInfoNextToCrosshair();

// [MobiusU9B] Traces straight out of source's eyes for up to range map units; actorMask picks which actors can stop it, 0 for walls only.
bool TraceFromView(AActor *source, DWORD actorMask, int range, FTraceResults &trace);

// [MobiusU9B] Draws the identify text, actor name and telemeter beside the crosshair; viewer is NULL when the shared identify checks say nothing may show.
void DrawBesideCrosshair(AActor *viewer, const TargetText *identify);

} } // namespace zx::crosshair

#endif
