// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Anonymous accounts: the decisions, with no crypto and no filesystem in them.
//
// Every player holds one secret, client-auth.key, and derives a separate account from it for each
// server operator. There is no account server, nothing is registered, and the same secret produces
// the same account at that operator forever.
//
// What this unit owns is the part that must be identical on both sides of the wire and is worth
// testing without a socket: where the key files live, what an account is called, and exactly which
// bytes get signed. The signing, hashing and key generation live in the glue beside it.

#ifndef ZX_IDENTITY_COMPUTE_H
#define ZX_IDENTITY_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// How many hex characters an account name is. Sixteen bytes of a digest, which is far past any
// collision anybody will meet and still short enough to read out over voice.
extern const size_t kAccountNameLength;

// The account name for a public key, given that key's digest. Lower case hex, truncated.
//
// Truncated rather than whole because this is an identifier people will see in chat and in
// databases, not a security boundary: the full public key is what actually gets verified, and a
// name that collides still cannot sign.
//
// Empty when the digest is too short to name anything, which is how a caller says its hash failed
// rather than inventing an account.
std::string AccountNameFromDigest(const std::vector<unsigned char> &digest);

// Where a local instance keeps its secret. Instance 0 is client-auth.key; every further instance
// on the same machine gets a numbered file, because two clients sharing one account would be
// refused by the server as a duplicate.
std::string ClientAuthKeyPath(const std::string &configRoot, int instance);

// Where a host keeps the identity its server presents. One per machine, so an operator running
// several servers offers one account namespace across all of them.
std::string ServerAuthKeyPath(const std::string &configRoot);

// [rc4l] The bytes a client signs to prove it holds the key, and the reason this is a function
// rather than a line in the netcode.
//
// It carries THREE things and drops any of them at its peril:
//
//   * a domain tag, so a signature made here can never be replayed into some later feature that
//     also signs things with the same key;
//   * the session id, which both ends derived from a key exchange with each other, so a signature
//     is worthless outside the connection it was made on;
//   * the server's public key, so it is plainly bound to who was being talked to.
//
// The session id is what defeats the attack the server-signs-first rule also guards: a malicious
// server that COPIED a real server's public key cannot relay a live player's proof onwards, because
// its session with the victim and its session with the real server are different exchanges.
std::string ClientProofMessage(const std::string &sessionIdHex, const std::string &serverKeyHex);

// The bytes a server signs to prove itself, before the client reveals anything at all.
//
// A different domain tag from the client's, so neither side's proof can ever be presented as the
// other's. That is not a hypothetical: without it, a signature harvested from a server could be
// offered back to it as a client's.
std::string ServerProofMessage(const std::string &clientNonceHex, const std::string &serverKeyHex);

// Hex, lower case, for anything that has to go on the wire or into a path. Empty for empty input.
std::string ToHex(const std::vector<unsigned char> &bytes);

// The reverse, refusing anything that is not an even-length run of hex digits.
bool FromHex(const std::string &hex, std::vector<unsigned char> &out);

} // namespace zx

#endif // ZX_IDENTITY_COMPUTE_H
