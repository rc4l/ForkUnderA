// [rc4l] Implementation of the pure signal-chaining decision. See signal_chain_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "signal_chain_compute.h"

namespace zx {

SignalChainAction ComputeSignalChainAction(bool prevIsSelf, bool prevUsesSiginfo,
	bool sigactionIsReal, bool handlerIsReal)
{
	// Chaining into ourselves would recurse forever; fall back to a normal fatal exit.
	if (prevIsSelf)
		return SignalChainAction::ReraiseDefault;

	// SA_SIGINFO decides which member of the (union) sigaction is the live one. sentry-native
	// installs its handler with SA_SIGINFO, so this is the branch that reaches it.
	if (prevUsesSiginfo)
		return sigactionIsReal ? SignalChainAction::CallSigInfo : SignalChainAction::ReraiseDefault;

	return handlerIsReal ? SignalChainAction::CallPlain : SignalChainAction::ReraiseDefault;
}

} // namespace zx
