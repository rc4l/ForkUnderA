// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] A bitfield of server flags, edited from either end: the switches or the number.
//
// dmflags and its five siblings are each a single integer whose bits have names. Servers and forum
// posts quote the NUMBER; players think in the names. This screen has to show both and keep them
// the same thing, so the number box and the row of switches are two views of one value rather than
// two settings that have to be kept in step.
//
// THE BITS WE CANNOT NAME ARE KEPT. A number pasted from a server running a build with a flag this
// one has never heard of still has that bit set, and dropping it would quietly change what the
// player pasted into something else. Editing a switch preserves every bit it is not about, so the
// unknown ones survive being edited and written back out. They are counted so the screen can say
// they are there rather than leaving somebody to wonder why the number is not what they pasted.
//
// Header-pure by the features/ rules -- no engine types. The table of names and bits is read off
// the engine's own cvars by the caller; see features/server-browser/zx_flagtable.h for why that is
// better than a list written out again here.

#ifndef ZX_FLAGSET_COMPUTE_H
#define ZX_FLAGSET_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// One named bit of one field.
struct FlagBit
{
	std::string name;			// the cvar, eg "sv_nojump"
	unsigned int bit;

	FlagBit() : bit(0) {}
	FlagBit(const std::string &n, unsigned int b) : name(n), bit(b) {}
};

// One field, its bits, and what it is currently set to.
struct FlagField
{
	std::string name;			// the cvar the bits live in, eg "dmflags"
	std::vector<FlagBit> bits;
	unsigned int value;

	FlagField() : value(0) {}
};

// Whether a named bit is set.
bool FlagIsOn(unsigned int value, unsigned int bit);

// `value` with `bit` turned on or off. Every other bit is left exactly as it was, which is what
// keeps the unknown ones alive across an edit.
unsigned int FlagSet(unsigned int value, unsigned int bit, bool on);

// Every bit this build has a name for.
unsigned int KnownMask(const std::vector<FlagBit> &bits);

// The bits of `value` that no name in `bits` accounts for, and how many there are.
unsigned int UnknownBits(unsigned int value, const std::vector<FlagBit> &bits);
int CountBits(unsigned int value);

// [rc4l] A number a player typed or pasted.
//
// Decimal only, because that is what every server and every forum post quotes. Leading and trailing
// space is forgiven -- pasting from a console line brings it along -- and anything else is refused
// outright rather than being read up to the first bad character: "123abc" is not 123, it is a
// mistake, and silently taking half of it is how somebody ends up hosting settings they never
// chose. An empty string is zero, which is what an empty box means.
bool ParseFlagNumber(const std::string &text, unsigned int &out);

// The number as it should appear in the box.
std::string FormatFlagNumber(unsigned int value);

// [rc4l] The order the fields are shown in, given the ones the engine reported.
//
// Fixed rather than alphabetical or discovery order: these six have been quoted in this order for
// twenty years, and somebody checking a number against a forum post reads down the list. A field
// the engine has that this does not name goes last rather than being dropped -- see the table
// header for why nothing is ever dropped.
std::vector<std::string> FlagFieldOrder(const std::vector<std::string> &found);

} // namespace zx

#endif // ZX_FLAGSET_COMPUTE_H
