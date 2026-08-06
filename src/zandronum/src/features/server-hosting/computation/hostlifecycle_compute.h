// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The states a server we own can be in, and what each event does to them.
//
// This exists as its own unit because the failure it prevents is not a wrong pixel. A host that is
// half-started -- process alive, never bound, client waiting forever -- looks identical to one that
// is merely slow, and the difference decides whether the player sees a spinner or a reason. Once the
// question is "which state are we in and what does this event mean", it is arithmetic, and arithmetic
// belongs somewhere it can be swept exhaustively rather than observed by hosting a server and
// waiting.
//
// THE ONE-WAY RULE. Once a host has stopped -- for any reason, including success -- no event moves
// it back. The process it described is gone, and a state machine that could re-enter Running would
// have the client reconnecting to a pid that no longer exists. Restarting is a NEW host, with a new
// process and a new secret.
//
// READINESS IS OBSERVED, NOT ASSUMED. A spawned process is not a listening server; the gap between
// them is where every interesting failure lives (port taken, missing WAD, bad argument). So Starting
// is its own state, left only by evidence -- the child says it is up, or it dies, or we run out of
// patience.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_HOSTLIFECYCLE_COMPUTE_H
#define ZX_HOSTLIFECYCLE_COMPUTE_H

#include <string>

namespace zx
{

enum class HostState
{
	Idle,			// nothing of ours is running
	Starting,	 	// process spawned, not yet known to be listening
	Running,		// listening; the client may connect
	Stopping,		// we asked it to go away and are waiting for it to
	Stopped,		// it is gone, on purpose
	Failed,			// it is gone, not on purpose -- `reason` says why
};

enum class HostEvent
{
	Spawned,		// the process exists
	ReadyObserved,	// it told us it is listening
	SpawnFailed,	// it could not be created at all
	ChildExited,	// it is no longer running, whatever we wanted
	StopRequested,	// the player is leaving, quitting, or joining elsewhere
	Timeout,		// Starting has gone on too long
};

// One host, as far as the state machine is concerned.
struct HostLifecycle
{
	HostState state;
	std::string reason;		// player-facing, set only on Failed
	int elapsedMs;			// time in the current state

	HostLifecycle() : state(HostState::Idle), elapsedMs(0) {}
};

// How long a spawned process gets to start listening before we give up on it. Generous, because a
// cold disk loading a large WAD set is slow and the player would rather wait than be told a lie.
extern const int kReadyTimeoutMs;

// How long a stopping process gets to exit before it is killed outright.
extern const int kStopTimeoutMs;

// True once nothing of ours is running and nothing will be. The caller uses this to know it may
// forget the process entirely.
bool IsHostFinished(HostState state);

// True while we hold a process, whether or not it is useful yet. This is the one that must gate
// teardown: a Starting host owns a pid exactly as much as a Running one does.
bool HostHoldsProcess(HostState state);

// True when a client may connect. Deliberately narrower than "the process is alive".
bool HostAcceptsClients(HostState state);

// Apply `event`. Unknown transitions leave the state alone rather than asserting -- events arrive
// from a process we do not control, and a duplicate exit notification must not be a crash.
HostLifecycle StepHostLifecycle(const HostLifecycle &host, HostEvent event, const std::string &detail);

// Advance the clock. Emits Timeout itself when a state has waited too long, so the caller does not
// have to know which states are timed.
HostLifecycle TickHostLifecycle(const HostLifecycle &host, int deltaMs);

// What to show the player for a state. Never empty, so a UI can print it unconditionally.
const char *HostStateSummary(HostState state);

// [rc4l] Turn a child's dying words into something a player can act on.
//
// The engine's own errors are written for whoever is reading a server console, and "Could not bind
// to port" is only half an answer to someone who never chose a port. Recognised causes get a
// sentence naming the thing the player can change; anything unrecognised is passed through rather
// than replaced, because a strange message is still better evidence than a generic one.
std::string ExplainHostFailure(const std::string &childOutput, int exitCode);

} // namespace zx

#endif // ZX_HOSTLIFECYCLE_COMPUTE_H
