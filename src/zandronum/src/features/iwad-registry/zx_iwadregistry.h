// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Fua IWAD registration, meaning taking a copy of an IWAD into the shared per-user store.
//
// The store itself is described in computation/iwadregistry_compute.h, and this is the part that
// touches the disk.

#ifndef ZX_IWADREGISTRY_H
#define ZX_IWADREGISTRY_H

#include <string>

namespace zx
{

// The ForkUnderA data root for this user, asked of the OS rather than assembled, which is what
// makes a redirected profile work.
std::string IwadRegistryRoot( void );

// Register `path` if it is not registered already, returning the path of the stored copy or "" for
// an unreadable file, a hash that failed, or no writable root.
//
// Idempotent by construction, the destination being the file's own digest, so it is safe to call on
// every IWAD the engine touches without keeping a list of what has been done.
std::string RegisterIwad( const char *path );

} // namespace zx

#endif // ZX_IWADREGISTRY_H
