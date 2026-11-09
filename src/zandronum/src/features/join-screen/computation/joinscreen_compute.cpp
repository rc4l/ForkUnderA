// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/join-screen/computation/joinscreen_compute.h"

#include "features/server-browser/computation/colortext_compute.h"

#include <algorithm>

namespace zx
{

JoinStage ComputeJoinStage(const JoinScreenInputs &in)
{
	if (!in.enabled)
		return JoinStage::None;

	if (in.clientMode)
	{
		switch (in.phase)
		{
		case ConnectionPhase::Connecting:			return JoinStage::Connecting;
		case ConnectionPhase::Authenticating:		return JoinStage::Authenticating;
		case ConnectionPhase::RequestingSnapshot:
		case ConnectionPhase::ReceivingSnapshot:	return JoinStage::ReceivingGame;
		default:									return JoinStage::None;
		}
	}

	return in.joinInFlight ? JoinStage::LoadingFiles : JoinStage::None;
}

float ComputeJoinStageProgress(JoinStage stage)
{
	switch (stage)
	{
	case JoinStage::LoadingFiles:	return 0.25f;
	case JoinStage::Connecting:		return 0.45f;
	case JoinStage::Authenticating:	return 0.65f;
	case JoinStage::ReceivingGame:	return 0.9f;
	default:						return 0.f;
	}
}

std::string ComputeJoinStatus(JoinStage stage, const std::string &serverName)
{
	const std::string name = StripColorCodes(serverName);
	const std::string where = name.empty() ? std::string("the server") : name;

	switch (stage)
	{
	case JoinStage::LoadingFiles:	return "Loading files for " + where;
	case JoinStage::Connecting:		return "Connecting to " + where;
	case JoinStage::Authenticating:	return "Verifying the map with " + where;
	case JoinStage::ReceivingGame:	return "Receiving the game from " + where;
	default:						return "";
	}
}

float ComputeJoinProgressStep(float shown, float target, int elapsedMs)
{
	if (target <= shown)
		return shown;

	// [Doruk-Aksoy] Closes about a tenth of the gap every 16 ms, so a stage jump takes a few hundred milliseconds.
	const float t = std::min(1.f, std::max(0, elapsedMs) / 160.f);
	const float next = shown + (target - shown) * t;
	return std::min(target, next);
}

JoinScreenLayout ComputeJoinScreenLayout(int screenW, int screenH, int lineH)
{
	JoinScreenLayout out;
	const int line = std::max(1, lineH);
	const int pad = std::max(2, line / 2);

	out.barH = std::max(4, line / 2);
	out.bandH = pad + line * 2 + pad + out.barH + pad + line + pad;
	out.bandY = screenH - out.bandH;

	out.titleY = out.bandY + pad;
	out.statusY = out.titleY + line;
	out.barY = out.statusY + line + pad;
	out.hintY = out.barY + out.barH + pad;

	out.barW = std::max(1, (screenW * 3) / 4);
	out.barX = (screenW - out.barW) / 2;
	return out;
}

bool ComputeJoinTextIsDark(int r, int g, int b)
{
	// [Doruk-Aksoy] Rec. 601 luma, the same weighting the engine's palette code uses for brightness.
	return (r * 299 + g * 587 + b * 114) > 128 * 1000;
}

} // namespace zx
