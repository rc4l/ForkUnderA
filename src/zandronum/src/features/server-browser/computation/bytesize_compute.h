// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] A file size, in as few characters as it can be said in.
//
// This goes next to a filename in a list that is already tight -- "brutalv21.wad (23mb)" -- so every
// character it spends is one the filename does not get. Hence the shape:
//
//   NO DECIMALS. "23mb" not "23.4mb". Nobody deciding whether to download a file cares about the
//   fraction, and the point being drawn is a rough magnitude, not an accounting figure.
//   TWO-CHARACTER UNITS. b, kb, mb, gb, tb -- lowercase, no space before them. "MiB" is more correct
//   and costs a third more width to tell the player something they did not ask.
//   ROUNDED, NOT TRUNCATED. 1.9mb reads as "2mb". Truncating would call it 1mb, which is the kind of
//   wrong that makes a download look half the size it is.
//
// Powers of 1024, because that is what every other tool a Doom player has open says.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_BYTESIZE_COMPUTE_H
#define ZX_BYTESIZE_COMPUTE_H

#include <string>

namespace zx
{

// e.g. 0 -> "0b", 900 -> "900b", 2048 -> "2kb", 24117248 -> "23mb".
//
// Rounding can carry: 1023.7 kb rounds to 1024, which is spelled "1mb" rather than "1024kb", because
// a unit is meant to keep the number under four digits.
std::string FormatByteSize( unsigned long long bytes );

} // namespace zx

#endif // ZX_BYTESIZE_COMPUTE_H
