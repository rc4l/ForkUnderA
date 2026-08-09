// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Which port the reachability check should be asking about, and when to admit it moved.
//
// A server asks for the port you set and takes the next free one if that is busy. Taking the next
// one is right: refusing to start because something else holds 10666 would be worse than starting
// somewhere else. What is not right is going quiet about it.
//
// The reachability check answers a question about ONE specific port. Before hosting there is only
// the port in the form, and it has to follow that field as it is edited, because you are asking
// whether the port you are about to use will work. Once a server exists the question changes: the
// only port worth knowing about is the one it actually holds.
//
// Miss that and the check becomes a liar in exactly the situation it exists for. A forwarded 10666
// with the server on 10670 reports "reachable" -- true of the port it tested, useless to the player,
// and it reads as "hosting is fine" while nobody outside can get in. A check that is confidently
// wrong is worse than no check, because it stops you looking.
//
// The drift is also worth saying out loud on its own. Landing on an unconfigured port is the single
// most likely reason a working setup stops working, it is invisible from inside the game, and the
// player is the only one who can fix it, since we cannot forward a port for them.
//
// Header-pure by the features/ rules: no engine types, no sockets.

#ifndef ZX_HOSTPORT_COMPUTE_H
#define ZX_HOSTPORT_COMPUTE_H

namespace zx
{

// `runningPort` is 0 when no server is held. Returns the port the reachability check should test:
// the running one whenever there is one, and the configured one otherwise.
int PortToCheck(int runningPort, int configuredPort);

// Whether to tell the player that hosting landed somewhere other than where they set it up. Only
// ever true while a server is actually running, since there is nothing to warn about beforehand.
bool PortDriftNeedsWarning(int runningPort, int configuredPort);

} // namespace zx

#endif // ZX_HOSTPORT_COMPUTE_H
