// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Deciding whether a server matches what was typed in the search box.
//
// Substring, case-insensitive, colour codes stripped from the name before anything is compared.
//
// The colour part matters more than it sounds. Server names carry TEXTCOLOR_ESCAPE sequences, so the
// raw bytes of a name that reads "Brutal Doom" on screen may be "\034dBrutal \034hDoom" -- and a
// player searching "brutal doom" would get nothing back, because the space they typed is not the
// space in the string. Matching against the same key the SORT uses means the list filters on what
// the player can actually see, which is the only thing they could have typed from.
//
// Empty query matches everything, so an empty box is not a filter -- it is the absence of one.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_SERVERSEARCH_COMPUTE_H
#define ZX_SERVERSEARCH_COMPUTE_H

#include <string>

namespace zx
{

// The query in the form the match is done in: folded to lowercase. Exposed because the caller wants
// to fold once per frame rather than once per server.
std::string SearchKey( const std::string &query );

// `name` is the raw server name, escapes and all. `key` is a query already through SearchKey.
bool ServerMatchesSearch( const std::string &name, const std::string &key );

} // namespace zx

#endif // ZX_SERVERSEARCH_COMPUTE_H
