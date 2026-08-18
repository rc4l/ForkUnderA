// [rc4l] Pure parsing for the server registry list (config/serverregistries.txt), engine-free so it is
// unit-tested off-engine.
//
// The list tells a CLIENT which server registries to query for servers. It confers no authority: a
// server registry answers with server addresses and nothing more, and the client then asks each
// server directly for its details. So a hostile or broken entry costs its own listings and cannot
// affect anything else -- which is why parsing is forgiving by design and skips bad lines rather
// than failing the file.
//
// The client fetches it over HTTPS from a CDN that caches the copy in the repo, so the body can also
// be a challenge or error page rather than a list. Nothing here trusts the transport: host syntax is
// validated strictly, so an HTML body yields ZERO entries and the caller can treat "parsed nothing"
// as a failed fetch and keep its cached list.
//
// Format (one server registry per line, # comments, blank lines ignored):
//   <host>[:port]   <display name>
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_SERVERREGISTRYLIST_COMPUTE_H
#define ZX_SERVERREGISTRYLIST_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

struct ServerRegistryEntry
{
	std::string host;   // never empty; port stripped off into `port`
	int         port;   // 0 when the line carried none, so the caller applies its own default
	std::string name;   // display name; may be empty
};

// True when `host` is syntactically a hostname (or dotted IPv4) we could resolve. Strict on purpose:
// it is what stops an HTML error body from parsing as a server registry, so it rejects anything
// outside letters, digits, hyphens and dots.
bool IsValidServerRegistryHost( const std::string &host );

// Parse the whole file. Bad lines are skipped, not fatal. Duplicate hosts are collapsed, keeping the
// first occurrence, so a fetched list that repeats an entry cannot produce a doubled browser.
// An empty result means "nothing usable here" -- callers must not commit it over a good cached list.
// [rc4l] `skippedOut`, when given, collects the entries that were thrown away.
//
// Skipping a bad line rather than failing the file is right -- one typo must not cost a player every
// registry they have. But it was SILENT, and that silence hid a real bug: a registry named by an IPv6
// address was dropped by a parser that could not read one, and the client fell back to its built-in
// list looking perfectly healthy. Two IPv6 tests "passed" that way.
//
// Collected rather than logged line by line, so the caller can say "3 entries skipped: x, y, z" once
// instead of turning a mistyped file into a wall of console.
std::vector<ServerRegistryEntry> ParseServerRegistryList( const std::string &text,
	std::vector<std::string> *skippedOut = 0 );

// Split a user-supplied comma-separated list (the cl_fua_serverregistry_list CVAR) the same way, so
// hand-typed entries and the shipped file agree on what "host:port" means. Also de-duplicated.
std::vector<ServerRegistryEntry> ParseServerRegistryCSV( const std::string &csv,
	std::vector<std::string> *skippedOut = 0 );

// Merge fetched/shipped entries with the user's, keeping order and dropping duplicate hosts. The
// user's entries come FIRST: someone who names a server registry explicitly should see it queried
// before whatever we shipped, and if the two disagree on a port, theirs wins.
std::vector<ServerRegistryEntry> MergeServerRegistryLists( const std::vector<ServerRegistryEntry> &user,
                                                           const std::vector<ServerRegistryEntry> &shipped );

} // namespace zx

#endif
