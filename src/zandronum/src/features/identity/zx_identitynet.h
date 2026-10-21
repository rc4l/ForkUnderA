// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The wire half of anonymous accounts, riding on the connection sequence itself.
//
// The exchange is three of the messages a join already sends, so a client that cannot prove who it
// is never reaches a player slot and never costs a snapshot:
//
//     CLCC_ATTEMPTCONNECTION      + client nonce + client ephemeral
//     SVCC_AUTHENTICATE           + server key + server ephemeral + signature over the nonce
//     CLCC_ATTEMPTAUTHENTICATION  + account key + signature over the session
//
// THE ORDER IS THE SECURITY, so the server signs first, before the client has named an account.
//
// Nothing here is delivered reliably, so every step is written to be asked for more than once.
// See computation/connectchallenge_compute.h for what that costs.

#ifndef ZX_IDENTITYNET_H
#define ZX_IDENTITYNET_H

#include "doomtype.h"

// Forward declared rather than included, because networkshared.h needs the engine types pulled in
// first and including it here would make this header depend on its own include order.
struct BYTESTREAM_s;
#include "features/identity/zx_identity.h"

// Server: read the client's nonce off a connection attempt and mint or replay its challenge.
bool SERVER_FuaAuthReadHello( ULONG ulClient, BYTESTREAM_s *pByteStream );

// Server: append this slot's stored challenge to the message already being written.
void SERVER_FuaAuthWriteChallenge( ULONG ulClient, BYTESTREAM_s *pByteStream );

// Server: read and judge the proof, establishing the account when it checks out.
bool SERVER_FuaAuthReadProof( ULONG ulClient, BYTESTREAM_s *pByteStream );

// Server: forget a slot's challenge, so the next occupant is issued its own.
void SERVER_FuaAuthClearChallenge( ULONG ulClient );

// Client: append our nonce and ephemeral key, minted once and reused across retries.
void CLIENT_FuaAuthWriteHello( BYTESTREAM_s *pByteStream );

// Client: check the server proved itself, then work out the account and the proof to answer with.
bool CLIENT_FuaAuthReadChallenge( BYTESTREAM_s *pByteStream );

// Client: append the account and the proof worked out above.
void CLIENT_FuaAuthWriteProof( BYTESTREAM_s *pByteStream );

// Client: forget the exchange when the connection ends.
void CLIENT_FuaAuthReset( void );

#endif // ZX_IDENTITYNET_H
