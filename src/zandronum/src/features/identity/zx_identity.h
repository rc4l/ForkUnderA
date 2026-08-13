// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The crypto and the key files behind anonymous accounts. Decisions live in
// computation/identity_compute.h; this is what touches OpenSSL and the disk.
//
// NOTHING HERE BLOCKS A RUNNING SERVER. Keys are read once at startup and kept in memory, so a
// handshake is arithmetic and nothing else: no disk, and above all no network. That is the whole
// reason the account server is gone rather than reimplemented. The old SRP path sent packets to
// auth.zandronum.com and waited, so a slow or dead auth server was a stalled join for everybody;
// there is no such wait left to have.
//
// An Ed25519 verify is on the order of tens of microseconds, so even a full server of players
// arriving at once costs less than a single frame. Signing is the same order.

#ifndef ZX_IDENTITY_H
#define ZX_IDENTITY_H

#include <string>
#include <vector>

namespace zx
{

typedef std::vector<unsigned char> Bytes;

// A keypair as this feature passes it around. The private half is a raw 32-byte Ed25519 seed.
struct KeyPair
{
	Bytes publicKey;
	Bytes privateKey;

	bool IsValid( void ) const { return ( publicKey.size( ) == 32 ) && ( privateKey.size( ) == 32 ); }
};

// [rc4l] Load this machine's client secret, generating it on first run.
//
// `instance` numbers concurrent clients on one machine: the second one cannot share the first's
// account, because the server refuses duplicates. Called once at startup.
bool Identity_InitClient( const char *configRoot, int instance );

// The same for the identity a hosted server presents. Generated on first host.
bool Identity_InitServer( const char *configRoot );

// This server's public key, for advertising to clients. Empty when hosting was never set up.
const Bytes &Identity_ServerPublicKey( void );

// [rc4l] The account this player uses at the server holding `serverPublicKey`.
//
// Derived rather than stored: one operator's servers share a key, so they see one account and one
// database, while unrelated operators see unrelated accounts and cannot correlate the same player.
KeyPair Identity_DeriveAccount( const Bytes &serverPublicKey );

// The account name for a public key: the truncated digest players and mods see.
std::string Identity_AccountName( const Bytes &publicKey );

// Ed25519 over arbitrary bytes. `privateKey` is the 32-byte seed.
bool Identity_Sign( const Bytes &privateKey, const std::string &message, Bytes &signatureOut );
bool Identity_Verify( const Bytes &publicKey, const std::string &message, const Bytes &signature );

// Where the key files live: the CONFIG directory, never the data one that holds the IWAD
// store, because that store is meant to be found by the engine file search.
std::string Identity_ConfigRoot( void );

// Cryptographically strong random bytes, for nonces. Never rand().
bool Identity_RandomBytes( size_t count, Bytes &out );

// One side of an X25519 exchange, which is what gives both ends a shared session id no third party
// can reproduce. That id is what binds a proof to its connection.
bool Identity_NewEphemeral( KeyPair &out );
bool Identity_SharedSession( const Bytes &ourPrivate, const Bytes &theirPublic, Bytes &sessionIdOut );

} // namespace zx

#endif // ZX_IDENTITY_H
