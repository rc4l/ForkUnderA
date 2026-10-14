// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The tally every check against GL's oracle keeps (the walks, the sky, portal views, sprites, the weapon, the light model): comparisons made, how many differed, and the first few described.

#ifndef ZX_ORACLE_COMPUTE_H
#define ZX_ORACLE_COMPUTE_H

#include <cstddef>
#include <string>

namespace zx { namespace frame {

struct OracleTally
{
	int checked;
	int differed;
	std::string firsts;   // [rc4l] the first differences' descriptions, a line each

	OracleTally() : checked(0), differed(0) {}
};

// [rc4l] Count a difference; true when it is among the first `keep`, which a caller then describes with ComputeOracleNote.
bool ComputeOracleDiffer(OracleTally &t, int keep);

// [rc4l] Whether a difference found inside one comparison may still be described: fewer than `keep` counted and fewer than maxChars written.
bool ComputeOracleRoom(const OracleTally &t, int keep, std::size_t maxChars);

// [rc4l] Append one printf-style line to the descriptions.
void ComputeOracleNote(OracleTally &t, const char *fmt, ...);

// [rc4l] Back to nothing counted, as a report leaves it.
void ComputeOracleReset(OracleTally &t);

}} // namespace zx::frame

#endif // ZX_ORACLE_COMPUTE_H
