// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Is this v6 address really a v4 one wearing a v6 hat?
//
// A dual-stack socket delivers every v4 peer as ::ffff:a.b.c.d, sixteen bytes rather than four. If
// that form reaches anything above the socket layer, the same machine becomes one address when it
// arrives on a dual-stack listener and a different one when it arrives on a v4 listener, and the two
// never compare equal.
//
// What breaks then is everything keyed on an address, and it breaks for exactly the players who are
// NOT using v6, which is most of them: bans miss, the registry lists one server twice, a punch cookie
// is claimed against an address nobody will match. All of it silent.
//
// So the check lives here, alone and tested, rather than inline in the socket layer where it cannot
// be. It is four lines of byte comparison and the cost of getting it subtly wrong is enormous, which
// is exactly the ratio that earns a unit of its own.
//
// The form is ten zero bytes, then 0xff 0xff, then the four v4 bytes (RFC 4291).
//
// Header-pure by the features/ rules: no engine types, no sockets.

#ifndef ZX_V6MAPPED_COMPUTE_H
#define ZX_V6MAPPED_COMPUTE_H

namespace zx
{

// Whether `sixteen` is a v4-mapped v6 address. `sixteen` must point at 16 readable bytes.
bool IsV4MappedV6( const unsigned char *sixteen );

// The embedded v4 address, written into `four`. Only meaningful when IsV4MappedV6 said yes, and
// deliberately separate from it so a caller cannot extract without having asked.
void ExtractMappedV4( const unsigned char *sixteen, unsigned char *four );

} // namespace zx

#endif // ZX_V6MAPPED_COMPUTE_H
