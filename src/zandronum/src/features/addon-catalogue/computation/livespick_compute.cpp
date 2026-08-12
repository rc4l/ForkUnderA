// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/addon-catalogue/computation/livespick_compute.h"

namespace zx
{

namespace
{

LivesShape ShapeOf(HostGameMode mode)
{
	switch (mode)
	{
	// Zero is the Cooperative gamemode itself. Asking for lives means becoming Survival, because
	// Cooperative does not carry GMF_USEMAXLIVES and ignores the cvar outright.
	case HostGameMode::Cooperative:
		return LivesShape::CoopSurvival;

	// The one mode where sv_maxlives 0 really is unlimited, by an explicit special case in
	// GAMEMODE_AreLivesLimited.
	case HostGameMode::Invasion:
		return LivesShape::Invasion;

	// Already a lives mode. Zero would be read as one by GAMEMODE_GetMaxLives, so one is the floor
	// and there is no unlimited to offer.
	case HostGameMode::Survival:
	case HostGameMode::LastManStanding:
	case HostGameMode::TeamLastManStanding:
		return LivesShape::Rounds;

	default:
		return LivesShape::None;
	}
}

int Clamp(int v, int lo, int hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

} // namespace

LivesControl LivesFor(HostGameMode mode, int wanted, int defaultLives, int maxLives)
{
	LivesControl out;
	out.shape = ShapeOf(mode);

	if (out.shape == LivesShape::None)
	{
		out.reason = (mode == HostGameMode::Unknown)
			? "This experience does not say how it plays"
			: "This way of playing has no lives";
		return out;
	}

	// [rc4l] A ceiling of zero is how an entry opts out whatever its gamemode. Some packs are built
	// around exactly one arrangement and a control offering to change it would be offering a
	// mistake.
	if (maxLives <= 0)
	{
		out.shape = LivesShape::None;
		out.reason = "This experience sets its own lives";
		return out;
	}

	out.applies = true;
	out.min = (out.shape == LivesShape::Rounds) ? 1 : 0;
	out.max = (maxLives < out.min) ? out.min : maxLives;

	// Nothing chosen takes the entry's default, which is also clamped: an entry asking for unlimited
	// in a mode that has no such thing gets the floor rather than a value the engine would silently
	// reinterpret.
	const int start = (wanted < 0) ? defaultLives : wanted;
	out.value = Clamp(start, out.min, out.max);
	out.unlimited = (out.value == 0);

	// One stop is not a choice. The cvars still get set from it, so the pack gets the lives it asked
	// for; there is simply no control to draw.
	out.adjustable = (out.min < out.max);

	return out;
}

std::vector<std::pair<std::string, std::string> > LivesCvars(const LivesControl &control)
{
	std::vector<std::pair<std::string, std::string> > out;

	if (!control.applies)
		return out;

	char number[16];
	// Small non-negative integers only, so this cannot truncate.
	snprintf(number, sizeof(number), "%d", control.value);

	switch (control.shape)
	{
	case LivesShape::CoopSurvival:
		// [rc4l] The gamemode IS the setting here. Unlimited co-op is Cooperative; any number of
		// lives is Survival. Both written every time rather than only the one that changed, because
		// the entry's own cfg has already set one of them and a control that only ever turns things
		// on cannot turn them off again.
		if (control.unlimited)
		{
			out.push_back(std::make_pair("survival", "false"));
			out.push_back(std::make_pair("cooperative", "true"));
		}
		else
		{
			out.push_back(std::make_pair("cooperative", "true"));
			out.push_back(std::make_pair("survival", "true"));
			out.push_back(std::make_pair("sv_maxlives", number));
		}
		return out;

	case LivesShape::Invasion:
	case LivesShape::Rounds:
		// The mode is already right; only the count is ours to set.
		out.push_back(std::make_pair("sv_maxlives", number));
		return out;

	default:
		return out;
	}
}

} // namespace zx
