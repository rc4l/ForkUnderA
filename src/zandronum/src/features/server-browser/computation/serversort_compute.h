// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The order servers appear in the browser.
//
// Busiest first, then alphabetically. The list used to sort by ping, which optimises for a number
// nobody chose a server on: a 12 ms server with nobody in it is not a better place to play than a
// 60 ms one with eleven people, and on a local network -- where every ping is 0 -- ping ordering
// degenerates into insertion order, which looks random every refresh.
//
// FULL SERVERS SORT TO THE TOP with everyone else, deliberately, rather than being pushed down for
// being unjoinable. A full server is evidence about where people play, which is what someone
// scanning this list is actually looking for, and the player count already reads red when it is
// full. Hiding the busiest servers because you cannot join them this second answers a question
// nobody asked.
//
// TWO THINGS THE NAME COMPARISON HAS TO HANDLE, both of which sorted wrongly before:
//
//   - Colour codes. Names carry TEXTCOLOR_ESCAPE sequences, and comparing raw bytes sorts on the
//     escape rather than the letters -- so a coloured server files under 0x1C, which is to say
//     before everything, in an order the player cannot see any reason for.
//   - Case. "brutal" and "Brutal" belong next to each other; ASCII puts every capital before every
//     lowercase letter, which scatters them.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_SERVERSORT_COMPUTE_H
#define ZX_SERVERSORT_COMPUTE_H

#include <string>

namespace zx
{

// The sortable form of a server name: colour codes removed, folded to lowercase. Exposed because it
// is worth testing on its own -- it is where both of the subtleties above live.
std::string ServerSortKey(const std::string &name);

// Negative if A comes first, positive if B does, 0 if they tie completely. More players wins; equal
// counts fall back to the name; identical names tie.
int CompareServers(int playersA, const std::string &nameA, int playersB, const std::string &nameB);

} // namespace zx

#endif // ZX_SERVERSORT_COMPUTE_H
