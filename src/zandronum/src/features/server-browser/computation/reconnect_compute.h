// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] What the reconnect command should do, which is not what it used to do.
//
// Joining from the browser asks the registry to get the server's router to let us in, and then
// connects without waiting on the answer. The reconnect command skipped all of that and went
// straight to the stored address, which works only while the mapping the original join opened is
// still alive. It is not, minutes later: a NAT mapping is measured in tens of seconds. So a player
// who dropped from a server behind a router could not reconnect at all, and had to go back to the
// browser to be introduced again -- the one place the punch was wired in.
//
// The rule is the same one the browser join follows, and the important half is what it does NOT do:
// asking never gates connecting. A registry that is down, a server too old to be punched, or a punch
// that simply fails all end exactly where the old behaviour ended, which is why this is safe to put
// in front of every reconnect rather than only the ones we think need it.
//
// Header-pure by the features/ rules: no engine types.

#ifndef ZX_RECONNECT_COMPUTE_H
#define ZX_RECONNECT_COMPUTE_H

namespace zx
{

enum class ReconnectAction
{
	Refuse,			// nothing stored to reconnect to, so say so rather than connect to nowhere
	AskThenConnect,	// ask for an introduction, then connect regardless of the answer
};

// Decide what a reconnect should do.
//
// Whether asking is worth it at all -- a server on this LAN has no router between us to open -- is
// deliberately not decided here. PunchRequestFor already owns that question and declines silently,
// and a second copy of the rule is a second place for it to be wrong.
ReconnectAction DecideReconnect( bool haveStoredAddress );

// Whether this action goes on to attempt the connection. True for everything but Refuse, and stated
// as its own question because the whole design rests on the punch never being able to stop a join.
bool ReconnectConnects( ReconnectAction action );

} // namespace zx

#endif // ZX_RECONNECT_COMPUTE_H
