// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Hashes of a file on disk, streamed, for the download gates.
//
// Two algorithms because the two questions are different, and only one of them is ours to choose:
//
//   SHA-256 -- the IWAD allowlist. This is a security gate: a hash that matches means "these exact
//     bytes are a game someone gave away". MD5 would be wrong here. Chosen-prefix MD5 collisions are
//     practical, so an attacker could craft a commercial IWAD colliding with a free one's digest and
//     walk straight through the only check that stops them. (Odamex uses MD5 for their equivalent,
//     but theirs is a DENYlist -- a collision there merely refuses something harmless.)
//
//   MD5 -- PWAD integrity against what the server advertised. Not a choice: Zandronum's launcher
//     protocol carries MD5 (network.cpp: `pwad.checksum = MD5Sum`), so that is what we compare
//     against. Acceptable, because forging it requires already controlling the server that told us
//     the hash -- at which point it is not a hash problem.
//
// Both stream the file in blocks rather than reading it whole: a WAD can be hundreds of megabytes,
// and the cap allows more.
//
// Thread-safety: these are the ONLY hashing entry points the download worker uses, and they touch
// nothing but stdio and OpenSSL. Deliberately not md5.h's MD5SumOfFile, which goes through the
// engine's FileReader -- see the threading note in zx_waddownload.cpp.

#ifndef ZX_FILEHASH_H
#define ZX_FILEHASH_H

namespace zx
{

// Lowercase hex SHA-256 of `path` into `outHex` (65 bytes: 64 + NUL). False on any read error, with
// outHex emptied -- never a partial digest a caller could mistake for a real one.
bool Sha256OfFile( const char *path, char *outHex, int outSize );

// Lowercase hex MD5 of `path` into `outHex` (33 bytes: 32 + NUL). Same failure contract.
bool Md5OfFile( const char *path, char *outHex, int outSize );

} // namespace zx

#endif // ZX_FILEHASH_H
