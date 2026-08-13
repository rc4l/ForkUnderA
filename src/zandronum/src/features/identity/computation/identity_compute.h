// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The decisions behind anonymous accounts, with no crypto and no filesystem in them.
//
// Every player holds one secret, client-auth.key, and derives a separate account from it for each
// server operator, with nothing registered anywhere.
//
// What this unit owns is the part that must be identical on both sides of the wire: where the key
// files live, what an account is called, and exactly which bytes get signed.

#ifndef ZX_IDENTITY_COMPUTE_H
#define ZX_IDENTITY_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// How many hex characters an account name is, being sixteen bytes of a digest, which is far past
// any collision anybody will meet and still short enough to read out over voice.
extern const size_t kAccountNameLength;

// The account name for a public key, as lower case hex truncated to kAccountNameLength.
//
// Truncated rather than whole because this is an identifier people read, not a security boundary,
// the full public key being what actually gets verified.
//
// Empty when the digest is too short to name anything, which is how a caller says its hash failed
// rather than inventing an account.
std::string AccountNameFromDigest(const std::vector<unsigned char> &digest);

// Where a local instance keeps its secret, instance 0 being client-auth.key and every further one
// on the same machine getting a numbered file, since a shared account is refused as a duplicate.
std::string ClientAuthKeyPath(const std::string &configRoot, int instance);

// Where a host keeps the identity its server presents, one per machine so an operator running
// several servers offers one account namespace across all of them.
std::string ServerAuthKeyPath(const std::string &configRoot);

// [rc4l] The bytes a client signs to prove it holds the key, which carry THREE things and drop any
// of them at their peril:
//
//   * a domain tag, so a signature made here can never be replayed into a later feature that also
//     signs with the same key;
//   * the session id, so a signature is worthless outside the connection it was made on;
//   * the server's public key, so it is plainly bound to who was being talked to.
//
// The session id is what stops a server that COPIED a real public key from relaying a live
// player's proof onwards, its two sessions being different exchanges.
std::string ClientProofMessage(const std::string &sessionIdHex, const std::string &serverKeyHex);

// The bytes a server signs to prove itself, before the client reveals anything at all.
//
// It carries a different domain tag from the client's, so a signature harvested from a server can
// never be offered back to it as a client's.
std::string ServerProofMessage(const std::string &clientNonceHex, const std::string &serverKeyHex);

// Lower case hex for anything that goes on the wire or into a path, empty for empty input.
std::string ToHex(const std::vector<unsigned char> &bytes);

// The reverse, refusing anything that is not an even-length run of hex digits.
bool FromHex(const std::string &hex, std::vector<unsigned char> &out);

} // namespace zx

#endif // ZX_IDENTITY_COMPUTE_H
