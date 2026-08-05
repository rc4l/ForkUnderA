// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Turning the six flag words a server sends into the names of what is actually switched on.
//
// This is not a loop over bits, and that is the whole reason it is a tested unit rather than four
// lines in the drawer. dmflags encodes falling damage as a two-bit FIELD, not as three flags:
//
//     DF_FORCE_FALLINGZD = 1 << 3   (8)
//     DF_FORCE_FALLINGHX = 2 << 3   (16)
//     DF_FORCE_FALLINGST = 3 << 3   (24)
//
// A server running Strife falling has both bits set, so a plain `flags & value` matches all three and
// the panel claims the server runs three mutually exclusive falling styles at once. Worse, it is not
// obviously broken -- it looks like a server with a lot of flags on.
//
// The rule that fixes it: try longer masks first, and once a mask matches, consider its bits spoken
// for. 24 matches, claims bits 3 and 4, and 8 and 16 then have nothing left of their own to match.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_DMFLAGNAMES_COMPUTE_H
#define ZX_DMFLAGNAMES_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// One entry of the generated table, repeated here so the computation does not depend on the
// generated header (which the tests would otherwise have to produce before they could compile).
struct FlagNameEntry
{
	int word;				// which of the six words this belongs to
	unsigned int value;		// its mask -- NOT necessarily a single bit
	const char *name;
};

// The names switched on across `words`, in table order. `entries` is the generated table; `words` is
// what the server sent, which may be shorter or longer than the table expects -- an entry naming a
// word the server did not send is skipped rather than read past the end.
std::vector<std::string> ComputeSetFlagNames(const FlagNameEntry *entries, int entryCount,
	const std::vector<int> &words);

} // namespace zx

#endif // ZX_DMFLAGNAMES_COMPUTE_H
