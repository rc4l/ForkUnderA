// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The half of the orphan guarantee that lives in the CHILD.
//
// The parent's mechanisms are better where they exist -- a Windows job object and Linux
// PR_SET_PDEATHSIG are enforced by the kernel and cannot be missed. But macOS has no PDEATHSIG, and
// on macOS the only thing that can notice the parent is gone is the child itself.
//
// So the server, when it was started by a game rather than by a person, watches the pid that started
// it and exits when that pid does. On macOS this IS the guarantee. On Windows and Linux it is a
// second lock on a door the kernel already bolted -- kept anyway, because it costs one sleeping
// thread and the failure it prevents is a server nobody can see still holding a port.
//
// Naming: fua_ per the ZandroX rule, since -host and friends are Zandronum's and this is ours.

#ifndef ZX_HOSTWATCHDOG_H
#define ZX_HOSTWATCHDOG_H

namespace zx
{

// Read -fua_hostparent and, if present, start watching. Call once, early, before the server does
// anything it would mind being interrupted in the middle of. Does nothing for a server a person
// started themselves.
void HostWatchdogInit( void );

} // namespace zx

#endif // ZX_HOSTWATCHDOG_H
