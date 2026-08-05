// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_filehash.h. OpenSSL rather than a vendored digest: it is already a hard dependency of
// this build (src/CMakeLists.txt does FIND_PACKAGE(OpenSSL REQUIRED) for csrp), so SHA-256 costs
// nothing to add and nothing to maintain. The engine's own md5.cpp is deliberately not used -- see
// the header for why the worker thread stays off engine types.

#include <cstdio>
#include <cstring>

#include <openssl/evp.h>

#include "features/wad-download/zx_filehash.h"

namespace
{

// 64 KB blocks: big enough that a several-hundred-megabyte WAD is not a syscall storm, small enough
// to sit on the worker's stack without thought.
const size_t kBlock = 64 * 1024;

// Hex, lowercase, so a digest can be compared to what sha256sum prints and to what the generated
// tables hold without either side having to normalise.
void ToHex( const unsigned char *digest, unsigned int len, char *outHex )
{
	static const char kHex[] = "0123456789abcdef";
	for ( unsigned int i = 0; i < len; ++i )
	{
		outHex[i * 2]     = kHex[( digest[i] >> 4 ) & 0xF];
		outHex[i * 2 + 1] = kHex[digest[i] & 0xF];
	}
	outHex[len * 2] = '\0';
}

bool DigestFile( const char *path, const EVP_MD *md, char *outHex, int outSize )
{
	if ( path == NULL || outHex == NULL || outSize <= 0 )
		return false;
	outHex[0] = '\0';

	// The caller's buffer has to hold the whole digest plus its terminator, or we would emit a
	// truncated hex string that could still compare equal to a truncated entry somewhere.
	const int needed = EVP_MD_size( md ) * 2 + 1;
	if ( outSize < needed )
		return false;

	FILE *fp = std::fopen( path, "rb" );
	if ( fp == NULL )
		return false;

	EVP_MD_CTX *ctx = EVP_MD_CTX_new( );
	if ( ctx == NULL )
	{
		std::fclose( fp );
		return false;
	}

	bool ok = ( EVP_DigestInit_ex( ctx, md, NULL ) == 1 );
	if ( ok )
	{
		unsigned char buf[kBlock];
		size_t n;
		while (( n = std::fread( buf, 1, sizeof buf, fp )) > 0 )
		{
			if ( EVP_DigestUpdate( ctx, buf, n ) != 1 )
			{
				ok = false;
				break;
			}
		}
		// A read that stopped short of EOF would digest a prefix of the file and report success --
		// the one failure mode that would produce a confident wrong answer.
		if ( ok && std::ferror( fp ) != 0 )
			ok = false;
	}

	if ( ok )
	{
		unsigned char digest[EVP_MAX_MD_SIZE];
		unsigned int len = 0;
		ok = ( EVP_DigestFinal_ex( ctx, digest, &len ) == 1 );
		if ( ok )
			ToHex( digest, len, outHex );
	}

	EVP_MD_CTX_free( ctx );
	std::fclose( fp );

	if ( !ok )
		outHex[0] = '\0';
	return ok;
}

} // namespace

namespace zx
{

bool Sha256OfFile( const char *path, char *outHex, int outSize )
{
	return DigestFile( path, EVP_sha256( ), outHex, outSize );
}

bool Md5OfFile( const char *path, char *outHex, int outSize )
{
	return DigestFile( path, EVP_md5( ), outHex, outSize );
}

} // namespace zx
