// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The crypto and the key files behind anonymous accounts, where the decisions live in
// computation/identity_compute.h and this is what touches OpenSSL and the disk.
//
// NOTHING HERE BLOCKS A RUNNING SERVER, because keys are read once at startup and a handshake is
// then arithmetic and nothing else.

#ifndef ZX_IDENTITY_H
#define ZX_IDENTITY_H

#include <string>
#include <vector>

namespace zx
{

typedef std::vector<unsigned char> Bytes;

// A keypair as this feature passes it around, the private half being a raw 32-byte Ed25519 seed.
struct KeyPair
{
	Bytes publicKey;
	Bytes privateKey;

	bool IsValid( void ) const { return ( publicKey.size( ) == 32 ) && ( privateKey.size( ) == 32 ); }
};

// [rc4l] Load this machine's client secret, generating it on first run, where `instance` numbers
// concurrent clients on one machine so the second cannot share the first's account.
bool Identity_InitClient( const char *configRoot, int instance );

// [rc4l] Load the first client secret no other copy on this machine has claimed, returning which
// one that was, so two engines running side by side each play as their own account.
int Identity_InitClientHere( const char *configRoot );

// [rc4l] Play as the next spare secret instead, for a player whose account is occupied. False when
// this machine has no spare left.
bool Identity_SwitchToSpare( void );

// The same for the identity a hosted server presents, generated on first host.
bool Identity_InitServer( const char *configRoot );

// This server's public key, empty when hosting was never set up.
const Bytes &Identity_ServerPublicKey( void );

// [rc4l] The account this player uses at the server holding `serverPublicKey`, derived rather than
// stored so unrelated operators cannot correlate the same player.
KeyPair Identity_DeriveAccount( const Bytes &serverPublicKey );

// The account name for a public key, being the truncated digest players and mods see.
std::string Identity_AccountName( const Bytes &publicKey );

// Sign as this SERVER, kept separate from the general signer so the server private key never has
// to be handed around to reach it.
bool Identity_SignAsServer( const std::string &message, Bytes &signatureOut );

// Ed25519 over arbitrary bytes, where `privateKey` is the 32-byte seed.
bool Identity_Sign( const Bytes &privateKey, const std::string &message, Bytes &signatureOut );
bool Identity_Verify( const Bytes &publicKey, const std::string &message, const Bytes &signature );

// Where the key files live, which is one folder per user shared by every copy of the engine.
std::string Identity_ConfigRoot( void );

// Move keys out of the per-install folder a build before this one wrote them to.
void Identity_MigrateLegacyRoot( void );

// Cryptographically strong random bytes for nonces, never rand().
bool Identity_RandomBytes( size_t count, Bytes &out );

// One side of an X25519 exchange, which gives both ends a shared session id that binds a proof to
// its connection.
bool Identity_NewEphemeral( KeyPair &out );
bool Identity_SharedSession( const Bytes &ourPrivate, const Bytes &theirPublic, Bytes &sessionIdOut );

} // namespace zx

#endif // ZX_IDENTITY_H
