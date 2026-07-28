// [rc4l] Pure decision logic for chaining a fatal-signal handler to the one that was installed
// before it. The MCP bridge's crash handler (mcp_crash.cpp) overwrites whatever SIGSEGV/etc.
// disposition was already there -- and on a real build that is sentry-native's crash handler. If
// the bridge handler just restored SIG_DFL and re-raised, sentry would never see the crash and no
// report would be captured (this is exactly the "crash on TNT MAP18 with a bot never reached
// sentry" bug, which reproduced only under the MCP bridge). The fix is to save the previous
// disposition and chain to it; this file decides HOW to invoke it, split out so it is unit-testable
// off-engine. No engine headers here (mirrors the mcp_crash overlay's "no engine headers" rule).
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_SIGNAL_CHAIN_COMPUTE_H
#define ZX_SIGNAL_CHAIN_COMPUTE_H

namespace zx {

// What to do with the crash once our handler has written its own log. The previous disposition is
// described by the booleans passed to ComputeSignalChainAction (derived from the saved sigaction).
enum class SignalChainAction
{
	ReraiseDefault, // restore SIG_DFL and re-raise -> the process dies the normal way
	CallSigInfo,    // invoke the previous 3-arg handler: prev.sa_sigaction(sig, info, ctx)
	CallPlain,      // invoke the previous 1-arg handler: prev.sa_handler(sig)
};

// Decide how to chain to the previously-installed handler.
//   prevIsSelf      -- the saved handler IS our own handler (would recurse) -> must not call it
//   prevUsesSiginfo -- the saved sigaction had SA_SIGINFO set (use sa_sigaction, not sa_handler)
//   sigactionIsReal -- sa_sigaction points at a real function (not null / SIG_DFL / SIG_IGN)
//   handlerIsReal   -- sa_handler points at a real function (not null / SIG_DFL / SIG_IGN)
// When there is no real previous handler to chain to, we re-raise the default so the crash still
// terminates the process (and any OS-level reporter still runs).
SignalChainAction ComputeSignalChainAction(bool prevIsSelf, bool prevUsesSiginfo,
	bool sigactionIsReal, bool handlerIsReal);

} // namespace zx

#endif // ZX_SIGNAL_CHAIN_COMPUTE_H
