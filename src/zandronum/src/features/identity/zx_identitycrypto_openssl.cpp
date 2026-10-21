// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Accounts' primitives through OpenSSL's EVP interface.

#include <openssl/evp.h>
#include <openssl/rand.h>

#include "features/identity/zx_identitycrypto.h"

namespace zx { namespace crypto {

bool RandomBytes( unsigned char *out, size_t count )
{
	return ( RAND_bytes( out, static_cast<int>( count )) == 1 );
}

bool Ed25519PublicFromSeed( const unsigned char seed[32], unsigned char publicKey[32] )
{
	EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key( EVP_PKEY_ED25519, NULL, seed, 32 );
	if ( pkey == NULL )
		return false;

	size_t len = 32;
	const bool bOk = ( EVP_PKEY_get_raw_public_key( pkey, publicKey, &len ) == 1 ) && ( len == 32 );

	EVP_PKEY_free( pkey );
	return bOk;
}

bool Ed25519Sign( const unsigned char seed[32], const unsigned char *message, size_t length, unsigned char signature[64] )
{
	EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key( EVP_PKEY_ED25519, NULL, seed, 32 );
	if ( pkey == NULL )
		return false;

	EVP_MD_CTX *ctx = EVP_MD_CTX_new( );
	bool bOk = false;

	if ( ctx != NULL )
	{
		size_t len = 64;
		bOk = ( EVP_DigestSignInit( ctx, NULL, NULL, NULL, pkey ) == 1 )
			&& ( EVP_DigestSign( ctx, signature, &len, message, length ) == 1 )
			&& ( len == 64 );

		EVP_MD_CTX_free( ctx );
	}

	EVP_PKEY_free( pkey );
	return bOk;
}

bool Ed25519Verify( const unsigned char publicKey[32], const unsigned char *message, size_t length,
	const unsigned char *signature, size_t signatureLength )
{
	EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key( EVP_PKEY_ED25519, NULL, publicKey, 32 );
	if ( pkey == NULL )
		return false;

	EVP_MD_CTX *ctx = EVP_MD_CTX_new( );
	bool bOk = false;

	if ( ctx != NULL )
	{
		bOk = ( EVP_DigestVerifyInit( ctx, NULL, NULL, NULL, pkey ) == 1 )
			&& ( EVP_DigestVerify( ctx, signature, signatureLength, message, length ) == 1 );

		EVP_MD_CTX_free( ctx );
	}

	EVP_PKEY_free( pkey );
	return bOk;
}

bool X25519NewKey( unsigned char publicKey[32], unsigned char privateKey[32] )
{
	EVP_PKEY *pkey = NULL;
	EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id( EVP_PKEY_X25519, NULL );
	if ( ctx == NULL )
		return false;

	bool bOk = ( EVP_PKEY_keygen_init( ctx ) == 1 ) && ( EVP_PKEY_keygen( ctx, &pkey ) == 1 );
	EVP_PKEY_CTX_free( ctx );

	if ( bOk )
	{
		size_t lenPub = 32, lenPriv = 32;
		bOk = ( EVP_PKEY_get_raw_public_key( pkey, publicKey, &lenPub ) == 1 )
			&& ( EVP_PKEY_get_raw_private_key( pkey, privateKey, &lenPriv ) == 1 );
	}

	if ( pkey != NULL )
		EVP_PKEY_free( pkey );

	return bOk;
}

bool X25519Shared( const unsigned char privateKey[32], const unsigned char theirPublic[32], unsigned char shared[32] )
{
	EVP_PKEY *ours = EVP_PKEY_new_raw_private_key( EVP_PKEY_X25519, NULL, privateKey, 32 );
	EVP_PKEY *theirs = EVP_PKEY_new_raw_public_key( EVP_PKEY_X25519, NULL, theirPublic, 32 );
	bool bOk = false;

	if (( ours != NULL ) && ( theirs != NULL ))
	{
		EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new( ours, NULL );

		if ( ctx != NULL )
		{
			size_t len = 32;
			bOk = ( EVP_PKEY_derive_init( ctx ) == 1 )
				&& ( EVP_PKEY_derive_set_peer( ctx, theirs ) == 1 )
				&& ( EVP_PKEY_derive( ctx, shared, &len ) == 1 )
				&& ( len == 32 );

			EVP_PKEY_CTX_free( ctx );
		}
	}

	if ( ours != NULL ) EVP_PKEY_free( ours );
	if ( theirs != NULL ) EVP_PKEY_free( theirs );

	return bOk;
}

}} // namespace zx::crypto
