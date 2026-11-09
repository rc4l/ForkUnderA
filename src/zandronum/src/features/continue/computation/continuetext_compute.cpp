// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/continue/computation/continuetext_compute.h"

#include "features/server-browser/computation/colortext_compute.h"

namespace zx
{

namespace
{

// [Doruk-Aksoy] "MAP01 (doom2.wad)", or the bare map when the wad that holds it is unknown.
std::string MapWithWad(const ContinueTextInputs &in)
{
	const std::string map = StripColorCodes(in.mapName);
	return in.mapWad.empty() ? map : map + " (" + StripColorCodes(in.mapWad) + ")";
}

// [Doruk-Aksoy] The server's name and address on two lines, or the address alone when it never told us its name.
std::string ServerLines(const ContinueTextInputs &in)
{
	const std::string name = StripColorCodes(in.serverName);
	if (name.empty() || (name == in.address))
		return in.address;
	return name + "\n" + in.address;
}

} // namespace

std::string ComputeContinueLabel(ContinueMode mode)
{
	switch (mode)
	{
	case ContinueMode::Disconnect:	return "Disconnect";
	case ContinueMode::Continue:	return "Continue";
	default:						return "";
	}
}

std::string ComputeContinueTooltip(const ContinueTextInputs &in)
{
	const ContinueTarget target = in.verdict.target;

	if (in.verdict.mode == ContinueMode::Disconnect)
	{
		const std::string first = "Disconnect from this server\n";
		if (target == ContinueTarget::Offline)
			return first + "then resume your last offline game: " + MapWithWad(in);
		if (target == ContinueTarget::Hosted)
			return first + "then host " + StripColorCodes(in.hostMap) + " again with your last settings";
		return first + "and return to the main menu";
	}

	if (in.verdict.mode != ContinueMode::Continue)
		return "";

	if (target == ContinueTarget::Server)
		return "Rejoin the last server you played on\n" + ServerLines(in);
	if (target == ContinueTarget::Hosted)
		return "Host your last game again\n" + StripColorCodes(in.hostMap) + ", with the same settings";
	return "Resume your last offline game where you quit\n" + MapWithWad(in);
}

} // namespace zx
