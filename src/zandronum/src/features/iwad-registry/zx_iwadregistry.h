// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Fua IWAD registration: taking a copy of an IWAD into the shared per-user store.
//
// The store is described in computation/iwadregistry_compute.h. This is the part that touches the
// disk: where the root is, hashing the file, and copying it in if it is not already there.

#ifndef ZX_IWADREGISTRY_H
#define ZX_IWADREGISTRY_H

#include <string>

namespace zx
{

// The ForkUnderA data root for this user, from the OS rather than assembled. Asking is what makes a
// redirected corporate profile work, and a redirected profile is not a rare setup.
std::string IwadRegistryRoot( void );

// Register `path` if it is not registered already. Returns the path of the stored copy, or "" when
// nothing was stored -- an unreadable file, a hash that failed, or no writable root.
//
// Idempotent by construction: the destination is the file's own digest, so registering the same
// file twice finds it already there and copies nothing. That is what makes it safe to call on every
// IWAD the engine touches without keeping a list of what has been done.
std::string RegisterIwad( const char *path );

} // namespace zx

#endif // ZX_IWADREGISTRY_H
