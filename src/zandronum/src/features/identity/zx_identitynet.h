// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The wire half of anonymous accounts. See zx_identitynet.cpp for why the order matters.

#ifndef ZX_IDENTITYNET_H
#define ZX_IDENTITYNET_H

#include "doomtype.h"

// Forward declared rather than included: networkshared.h needs the engine types pulled in first,
// so including it here would make this header depend on its own include order.
struct BYTESTREAM_s;
#include "features/identity/zx_identity.h"

// Server: handle CLC_FUA_AUTH_HELLO and CLC_FUA_AUTH_PROOF.
bool SERVER_ProcessFuaAuthCommand( LONG lCommand, BYTESTREAM_s *pByteStream );

// Server: answer a hello, proving ourselves before the client reveals anything.
void SERVERCOMMANDS_FuaAuthChallenge( const ULONG ulClient, const zx::Bytes &serverPublic,
	const zx::Bytes &ephemeralPublic, const zx::Bytes &signature );

void CLIENTCOMMANDS_FuaAuthHello( const zx::Bytes &nonce, const zx::Bytes &ephemeralPublic );
void CLIENTCOMMANDS_FuaAuthProof( const zx::Bytes &accountPublic, const zx::Bytes &signature );

// Client: open the exchange, and answer the challenge once the server has proved itself.
void CLIENT_FuaAuthSendHello( void );
void CLIENT_FuaAuthHandleChallenge( BYTESTREAM_s *pByteStream );

#endif // ZX_IDENTITYNET_H
