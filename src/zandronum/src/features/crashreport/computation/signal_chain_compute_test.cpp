// [rc4l] Tests for the pure signal-chaining decision (signal_chain_compute).
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include "features/crashreport/computation/signal_chain_compute.h"

using namespace zx;

// The case that matters most: sentry-native installs an SA_SIGINFO handler. When the MCP bridge
// handler was layered on top, it must chain into that sa_sigaction so the crash is captured. This
// is the regression guard for "TNT MAP18 bot crash never reached sentry under the bridge".
TEST(SignalChain, SiginfoPreviousHandlerIsCalled)
{
	EXPECT_EQ(ComputeSignalChainAction(/*prevIsSelf=*/false, /*prevUsesSiginfo=*/true,
				  /*sigactionIsReal=*/true, /*handlerIsReal=*/false),
		SignalChainAction::CallSigInfo);
}

TEST(SignalChain, PlainPreviousHandlerIsCalled)
{
	EXPECT_EQ(ComputeSignalChainAction(false, /*siginfo=*/false, /*sigactionReal=*/false,
				  /*handlerReal=*/true),
		SignalChainAction::CallPlain);
}

// A self-referential save (we somehow recorded our own handler) must NOT recurse.
TEST(SignalChain, SelfNeverChains)
{
	EXPECT_EQ(ComputeSignalChainAction(/*prevIsSelf=*/true, true, true, true),
		SignalChainAction::ReraiseDefault);
	EXPECT_EQ(ComputeSignalChainAction(/*prevIsSelf=*/true, false, false, true),
		SignalChainAction::ReraiseDefault);
}

// No real previous handler (SIG_DFL / SIG_IGN / null) -> re-raise so the process still dies and any
// OS-level crash reporter still runs.
TEST(SignalChain, SiginfoButNoRealSigactionReraises)
{
	EXPECT_EQ(ComputeSignalChainAction(false, /*siginfo=*/true, /*sigactionReal=*/false, true),
		SignalChainAction::ReraiseDefault);
}

TEST(SignalChain, PlainButNoRealHandlerReraises)
{
	EXPECT_EQ(ComputeSignalChainAction(false, /*siginfo=*/false, true, /*handlerReal=*/false),
		SignalChainAction::ReraiseDefault);
}

// SA_SIGINFO takes precedence over the sa_handler slot regardless of what handlerIsReal says
// (they alias the same union in a real sigaction, so only the flagged member is meaningful).
TEST(SignalChain, SiginfoFlagSelectsSigactionSlot)
{
	EXPECT_EQ(ComputeSignalChainAction(false, /*siginfo=*/true, /*sigactionReal=*/true,
				  /*handlerReal=*/false),
		SignalChainAction::CallSigInfo);
	EXPECT_EQ(ComputeSignalChainAction(false, /*siginfo=*/false, /*sigactionReal=*/false,
				  /*handlerReal=*/true),
		SignalChainAction::CallPlain);
}
