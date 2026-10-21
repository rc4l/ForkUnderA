// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The primitives accounts are built from: OpenSSL's (zx_identitycrypto_openssl.cpp), or in a build without accounts none (zx_identitycrypto_none.cpp), where each reports failure.

#ifndef ZX_IDENTITYCRYPTO_H
#define ZX_IDENTITYCRYPTO_H

#include <stddef.h>

namespace zx { namespace crypto {

// [rc4l] Cryptographically strong random bytes, never rand().
bool RandomBytes( unsigned char *out, size_t count );

// [rc4l] The public half of an Ed25519 key from its 32-byte seed.
bool Ed25519PublicFromSeed( const unsigned char seed[32], unsigned char publicKey[32] );

// [rc4l] Ed25519 over `message`, signed with the 32-byte seed into the 64-byte signature.
bool Ed25519Sign( const unsigned char seed[32], const unsigned char *message, size_t length, unsigned char signature[64] );
bool Ed25519Verify( const unsigned char publicKey[32], const unsigned char *message, size_t length,
	const unsigned char *signature, size_t signatureLength );

// [rc4l] A fresh X25519 keypair, and the raw secret it shares with another's public key.
bool X25519NewKey( unsigned char publicKey[32], unsigned char privateKey[32] );
bool X25519Shared( const unsigned char privateKey[32], const unsigned char theirPublic[32], unsigned char shared[32] );

}} // namespace zx::crypto

#endif // ZX_IDENTITYCRYPTO_H
