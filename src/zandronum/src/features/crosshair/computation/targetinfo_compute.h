// [MobiusU9B] Where the target info beside the crosshair goes, when it fades, and what the actor-name line says, kept engine-free so they are unit-tested.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#ifndef ZX_TARGETINFO_COMPUTE_H
#define ZX_TARGETINFO_COMPUTE_H

#include "features/crosshair/computation/crosshair_compute.h"

#include <string>
#include <vector>

namespace zx { namespace crosshair {

// [MobiusU9B] cl_fua_identify_position: where Zandronum's identify text goes.
const int kTargetInfoAtBottom = 0;
const int kTargetInfoNextToCrosshair = 1;

// [MobiusU9B] cl_fua_identify_actorname: what the actor-name line shows.
const int kActorNameOff = 0;
const int kActorNameClass = 1;
const int kActorNameTag = 2;

// [MobiusU9B] How far an element's offset sliders can push it from its stacked place, in HUD-text pixels.
const int kTargetInfoMaxOffset = 200;

// [MobiusU9B] A point in the virtual (HUD-text) space the info is drawn in.
struct InfoPos
{
	int x;
	int y;
};

// [MobiusU9B] The size of that virtual space.
struct InfoCanvas
{
	int width;
	int height;
};

// [MobiusU9B] One stacked block of text beside the crosshair: hidden ones take no rows.
struct InfoElement
{
	bool visible;
	int lines;
	int offsetX;
	int offsetY;
};

// [MobiusU9B] value clamped to [lo, hi], shared by every integer setting this feature reads.
int ComputeCrosshairClampInt(int value, int lo, int hi);

// [MobiusU9B] Real pixels per HUD-text pixel, so the info scales like chat; 1 when hudHeight is not positive.
double ComputeCrosshairTextScale(int screenHeight, int hudHeight);

// [MobiusU9B] The virtual canvas that makes text drawn on it come out at textScale.
InfoCanvas ComputeCrosshairInfoCanvas(int screenWidth, int screenHeight, double textScale);

// [MobiusU9B] Where the first line goes: gap pixels right of the crosshair, centred on it vertically, in virtual coordinates.
InfoPos ComputeCrosshairInfoAnchor(const CrosshairRect &rect, double textScale, int lineHeight, int gap);

// [MobiusU9B] Stacks the visible elements below the anchor in order, then adds each one's own offsets.
std::vector<InfoPos> ComputeCrosshairInfoLayout(const std::vector<InfoElement> &elements, InfoPos anchor, int lineHeight);

// [MobiusU9B] How many lines text draws as, 0 for none.
int ComputeLineCount(const char *text);

// [MobiusU9B] Opacity elapsedTics after the text was last seen: full for holdTics, then a linear fade over fadeTics.
double ComputeIdentifyFadeAlpha(int elapsedTics, int holdTics, int fadeTics);

// [MobiusU9B] Whether a target's health is real on this machine: always offline, online only when the server shares that player's.
bool ComputeIdentifyHealthKnown(bool inClientMode, bool serverSharesHealth);

// [MobiusU9B] Whether the actor-name line may show: the player asked for it and neither server switch forbids it.
bool ComputeActorNameAllowed(int mode, bool noIdentifyTarget, bool noIdentifyActors);

// [MobiusU9B] The actor-name line for mode: the DECORATE class, the tag, or nothing.
std::string ComputeActorNameText(int mode, const char *tag, const char *className);

// [MobiusU9B] Whether the actor-name scan should look past what it hit: only actors the renderer would not draw.
bool ComputeActorScanPassesThrough(bool hitActor, bool invisible);

} } // namespace zx::crosshair

#endif
