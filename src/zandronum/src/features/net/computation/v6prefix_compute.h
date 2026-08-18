// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Matching a v6 address against a ban, which is a PREFIX rather than a pattern.
//
// The v4 ban list stores four decimal fields so a rule can be written 1.2.3.* and matched a field at
// a time. That shape does not survive the move to v6, and not because of the group count: it is that
// the unit people actually ban is different. An ISP hands a household a /64 and a household is what
// gets banned, so the rule is "these first N bits" and N is a number, not a wildcard.
//
// So a v6 ban is sixteen bytes and a length, and matching is a bit comparison. 2001:db8::/32 covers
// everything under it, /128 is one exact address, and /0 would be everybody, which is why parsing
// refuses a bare "::" without a length rather than quietly banning the internet.
//
// Kept apart from the v4 list rather than folded into it. Wildcards and prefixes are different
// questions, one file has to keep answering the old one for every rule already written, and a single
// type that pretended to do both would be doing neither clearly.
//
// Header-pure by the features/ rules: no engine types.

#ifndef ZX_V6PREFIX_COMPUTE_H
#define ZX_V6PREFIX_COMPUTE_H

namespace zx
{

// Whether `address` (16 bytes) falls inside `prefix` (16 bytes) for its first `bits`.
//
// `bits` outside 0 to 128 matches nothing: an out-of-range length is a broken rule, and a broken ban
// rule must fail to the side of banning nobody rather than banning everybody.
bool V6AddressInPrefix( const unsigned char *address, const unsigned char *prefix, int bits );

// Parse "2001:db8::/64" OR "2001:db8:*" into `prefix` and `bits`.
//
// The asterisk is the notation the v4 list already uses, and a v6 group is sixteen bits, so a star
// after N groups is a prefix of N*16. It is rewritten into the slash form and handed to the same
// parser, so one matcher answers both spellings and neither can be broken on its own. A bare "*" is
// refused: it means everybody, which nobody types on purpose. False when there is no length, when the length is
// out of range, or when the address half is not a v6 literal.
//
// A MISSING LENGTH IS REFUSED rather than defaulted. Reading a bare address as /128 would be the
// friendly guess, but reading it as /0 is the one that bans everybody, and a format where the two
// differ by an omission is one where the dangerous reading happens by accident. Say the number.
bool ParseV6Prefix( const char *text, unsigned char *prefix, int *bits );

// [rc4l] Render `prefix`/`bits` back to "2001:db8::/64" in `out` (48 bytes), which MUST round-trip
// through ParseV6Prefix or a rule widens every time the ban file is saved.
bool FormatV6Prefix( const unsigned char *prefix, int bits, char *out, int outSize );

} // namespace zx

#endif // ZX_V6PREFIX_COMPUTE_H
