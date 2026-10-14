// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] zx_filehash.h's hashing through the in-tree digests (computation/digest_compute.h), so a build without OpenSSL, the browser's, still identifies its wads.

#include <cstdio>
#include <cstring>
#include <string>

#include "features/wad-download/zx_filehash.h"
#include "features/wad-download/computation/digest_compute.h"

namespace
{

// 64 KB blocks: big enough that a several-hundred-megabyte WAD is not a syscall storm, small enough
// to sit on the worker's stack without thought.
const size_t kBlock = 64 * 1024;

// [rc4l] Stream `path` through one digest, then write its final bytes as hex.
template <typename State, size_t Len>
bool DigestFile( const char *path, char *outHex, int outSize, void ( *init )( State & ),
	void ( *update )( State &, const void *, size_t ), void ( *finish )( State &, uint8_t * ))
{
	if ( path == NULL || outHex == NULL || outSize <= 0 )
		return false;
	outHex[0] = '\0';

	// The caller's buffer has to hold the whole digest plus its terminator, or we would emit a
	// truncated hex string that could still compare equal to a truncated entry somewhere.
	if ( outSize < (int)( Len * 2 + 1 ))
		return false;

	FILE *fp = std::fopen( path, "rb" );
	if ( fp == NULL )
		return false;

	State s;
	init( s );
	unsigned char buf[kBlock];
	size_t n;
	while (( n = std::fread( buf, 1, sizeof buf, fp )) > 0 )
		update( s, buf, n );
	// A read that stopped short of EOF would digest a prefix of the file and report success --
	// the one failure mode that would produce a confident wrong answer.
	const bool ok = std::ferror( fp ) == 0;
	std::fclose( fp );
	if ( !ok )
		return false;

	uint8_t digest[Len];
	finish( s, digest );
	const std::string hex = zx::ComputeDigestHex( digest, Len );
	std::memcpy( outHex, hex.c_str( ), hex.size( ) + 1 );
	return true;
}

} // namespace

namespace zx
{

bool Sha256OfFile( const char *path, char *outHex, int outSize )
{
	return DigestFile<Sha256State, 32>( path, outHex, outSize, ComputeSha256Init, ComputeSha256Update, ComputeSha256Final );
}

bool Md5OfFile( const char *path, char *outHex, int outSize )
{
	return DigestFile<Md5State, 16>( path, outHex, outSize, ComputeMd5Init, ComputeMd5Update, ComputeMd5Final );
}

} // namespace zx
