// [MobiusU9B] See targetinfo_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/crosshair/computation/targetinfo_compute.h"

#include <cmath>

namespace zx { namespace crosshair {

namespace
{
	// [MobiusU9B] A scale that cannot divide by zero or flip the layout, should a caller pass a bad one.
	double UsableScale(double textScale)
	{
		return textScale > 0.0 ? textScale : 1.0;
	}
}

IdentifyLines ComputeIdentifyLines(const IdentifyToggles &toggles, const IdentifyTarget &target)
{
	const bool show = target.isPlayer ? toggles.players : (target.isMonster && toggles.monsters);
	if (!show) {
		return IdentifyLines{false, false, false, false, false, false};
	}
	const bool allyPlayer = target.friendly && target.isPlayer;
	return IdentifyLines{
		true,
		toggles.health && target.friendly && target.healthKnown,
		toggles.weapon && allyPlayer && target.hasWeapon,
		toggles.playerClass && allyPlayer,
		toggles.ghost && target.isMonster && target.isGhost,
		toggles.allyEnemy,
	};
}

int ComputeCrosshairClampInt(int value, int lo, int hi)
{
	if (value < lo) {
		return lo;
	}
	if (value > hi) {
		return hi;
	}
	return value;
}

double ComputeTargetInfoClampTextSize(double size)
{
	if (std::isnan(size)) {
		return 1.0;
	}
	if (size < kTargetInfoMinTextSize) {
		return kTargetInfoMinTextSize;
	}
	if (size > kTargetInfoMaxTextSize) {
		return kTargetInfoMaxTextSize;
	}
	return size;
}

double ComputeCrosshairTextScale(int screenHeight, int hudHeight, double textSize)
{
	const double hudScale = (hudHeight <= 0 || screenHeight <= 0) ? 1.0 : static_cast<double>(screenHeight) / hudHeight;
	return hudScale * ComputeTargetInfoClampTextSize(textSize);
}

InfoCanvas ComputeCrosshairInfoCanvas(int screenWidth, int screenHeight, double textScale)
{
	const double scale = UsableScale(textScale);
	return InfoCanvas{
		static_cast<int>(std::lround(screenWidth / scale)),
		static_cast<int>(std::lround(screenHeight / scale)),
	};
}

InfoPos ComputeCrosshairInfoAnchor(const CrosshairRect &rect, double textScale, int lineHeight, int gap)
{
	// [MobiusU9B] The image is centred on rect.x/y by its own offsets, so its right edge is half its width away.
	const double scale = UsableScale(textScale);
	const double rightEdge = rect.x + (rect.width / 2.0);
	return InfoPos{
		static_cast<int>(std::lround(rightEdge / scale)) + gap,
		static_cast<int>(std::lround(rect.y / scale)) - (lineHeight / 2),
	};
}

std::vector<InfoPos> ComputeCrosshairInfoLayout(const std::vector<InfoElement> &elements, InfoPos anchor, int lineHeight)
{
	std::vector<InfoPos> positions;
	positions.reserve(elements.size());
	int row = 0;
	for (const InfoElement &element : elements) {
		if (!element.visible) {
			positions.push_back(anchor);
			continue;
		}
		positions.push_back(InfoPos{anchor.x + element.offsetX, anchor.y + (row * lineHeight) + element.offsetY});
		row += element.lines;
	}
	return positions;
}

int ComputeLineCount(const char *text)
{
	if (text == nullptr || *text == '\0') {
		return 0;
	}
	int lines = 1;
	for (const char *c = text; *c != '\0'; ++c) {
		if (*c == '\n') {
			++lines;
		}
	}
	return lines;
}

double ComputeIdentifyFadeAlpha(int elapsedTics, int holdTics, int fadeTics)
{
	if (elapsedTics < 0) {
		return 0.0;
	}
	if (elapsedTics <= holdTics) {
		return 1.0;
	}
	const int fading = elapsedTics - holdTics;
	if (fading >= fadeTics) {
		return 0.0;
	}
	return 1.0 - (static_cast<double>(fading) / fadeTics);
}

bool ComputeIdentifyHealthKnown(bool inClientMode, bool serverSharesHealth)
{
	return !inClientMode || serverSharesHealth;
}

bool ComputeActorNameAllowed(int mode, bool noIdentifyTarget, bool noIdentifyActors)
{
	return mode != kActorNameOff && !noIdentifyTarget && !noIdentifyActors;
}

std::string ComputeActorNameText(int mode, const char *tag, const char *className)
{
	const char *text = nullptr;
	if (mode == kActorNameClass) {
		text = className;
	} else if (mode == kActorNameTag) {
		text = tag;
	}
	return text != nullptr ? std::string(text) : std::string();
}

bool ComputeActorNameCoversTarget(bool isPlayer, bool isMonster, bool shootable)
{
	// [MobiusU9B] The identify scan only stops on shootable actors, so a corpse is left to the actor name.
	return !((isPlayer || isMonster) && shootable);
}

} } // namespace zx::crosshair
