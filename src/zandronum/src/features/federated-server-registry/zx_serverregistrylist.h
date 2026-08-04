// [rc4l] Where the client's list of server registries comes from.
//
// Three sources, in priority order, merged by MergeServerRegistryLists:
//   1. cl_fua_serverregistry_list -- whatever the player typed. Always honoured, always first.
//   2. the cached copy of serverregistries.txt, refreshed over HTTPS at most every 6 hours.
//   3. the list compiled into this binary, used when there is no cache yet.
//
// The fetch goes to a CDN that caches the copy in the repo, so the source of truth is a file edited
// by pull request while GitHub never sees per-player traffic. Only the list of server REGISTRIES
// travels this way; server lists themselves are always UDP, straight from each server registry.
//
// Every failure mode collapses to "use what we already had". A timeout, a 404, a bot-challenge page,
// a truncated body: all of them parse to zero entries, and zero entries is never committed over a
// good cache. A player can lose the ability to DISCOVER new server registries, never the ability to
// reach the ones they already know.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_SERVERREGISTRYLIST_H
#define ZX_SERVERREGISTRYLIST_H

#include <vector>

#include "features/federated-server-registry/computation/serverregistrylist_compute.h"

namespace zx
{

// The full list to query, in order. Never empty in practice: the compiled-in default is the floor.
std::vector<ServerRegistryEntry> ServerRegistryList_Resolve( const char *userCSV );

// Start a background refresh if the cache is missing or older than 6 hours, and log the outcome of
// the PREVIOUS refresh if it has one to report. Non-blocking; main thread only.
void ServerRegistryList_MaybeRefresh( void );

} // namespace zx

#endif
