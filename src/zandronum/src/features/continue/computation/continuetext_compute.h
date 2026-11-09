// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] What the Continue pill says and what its tooltip promises, from the verdict that also decides what pressing it does.
#ifndef ZX_CONTINUETEXT_COMPUTE_H
#define ZX_CONTINUETEXT_COMPUTE_H

#include <string>

#include "features/continue/computation/continuebutton_compute.h"

namespace zx
{

// [Doruk-Aksoy] The facts about the chosen record a tooltip names; names may carry colour codes, which are stripped.
struct ContinueTextInputs
{
	ContinueButtonVerdict verdict;
	std::string serverName;
	std::string address;
	std::string mapName;
	std::string mapWad;
	std::string hostMap;
};

// [Doruk-Aksoy] "Disconnect" while in a server, "Continue" out of one, empty when hidden.
std::string ComputeContinueLabel(ContinueMode mode);

// [Doruk-Aksoy] A first line saying what a press does now and a second saying where it lands, empty when hidden.
std::string ComputeContinueTooltip(const ContinueTextInputs &in);

} // namespace zx

#endif // ZX_CONTINUETEXT_COMPUTE_H
