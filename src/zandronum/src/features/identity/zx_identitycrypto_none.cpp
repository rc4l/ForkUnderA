// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] A build without accounts (ZX_ACCOUNTS off, the browser's): no key is made or used, so the identity it would load reports failure and online play goes unsigned.

#include "features/identity/zx_identitycrypto.h"

namespace zx { namespace crypto {

bool RandomBytes( unsigned char *, size_t ) { return false; }
bool Ed25519PublicFromSeed( const unsigned char *, unsigned char * ) { return false; }
bool Ed25519Sign( const unsigned char *, const unsigned char *, size_t, unsigned char * ) { return false; }
bool Ed25519Verify( const unsigned char *, const unsigned char *, size_t, const unsigned char *, size_t ) { return false; }
bool X25519NewKey( unsigned char *, unsigned char * ) { return false; }
bool X25519Shared( const unsigned char *, const unsigned char *, unsigned char * ) { return false; }

}} // namespace zx::crypto
