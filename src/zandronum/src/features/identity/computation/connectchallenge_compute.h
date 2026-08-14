// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The rules that let the identity exchange ride on a connection sequence with no acks.
//
// Nothing before CTS_ACTIVE is delivered reliably. A client sits in one state re-sending the same
// request every three seconds until the server's answer moves it along, so the server is asked for
// the same thing many times and may be answered out of order.
//
// The exchange therefore cannot mint a fresh challenge per packet. A client that signed against a
// challenge the server has since replaced would be refused for having a bad link, which on a link
// that keeps dropping is a refusal no amount of retrying can clear.
//
// Header-pure by the features/ rules, so this decides and the caller does the crypto.

#ifndef ZX_CONNECTCHALLENGE_COMPUTE_H
#define ZX_CONNECTCHALLENGE_COMPUTE_H

#include <cstddef>

namespace zx
{

// The field widths the exchange puts on the wire, checked before any of it reaches OpenSSL.
extern const size_t kNonceBytes;
extern const size_t kKeyBytes;
extern const size_t kSignatureBytes;

// What to do with the challenge for a slot that has just been asked to connect.
enum ChallengeAction
{
	// Nothing has been issued to this slot, so make one.
	CHALLENGE_MINT,

	// One exists and must go back out unchanged, however many times it is asked for.
	CHALLENGE_REPLAY,
};

// [rc4l] Mint only for a slot that has no challenge, or one whose caller is not the same client.
//
// Keyed on the client's nonce rather than its address, because the nonce is stable across that
// client's own retries and an address is not: a NAT that rebinds a port mid-connect would look
// like a new occupant, mint a second challenge, and leave a late copy of the first one able to
// make the client sign against an ephemeral key we no longer hold.
ChallengeAction ChallengeActionForAttempt(bool hasStoredChallenge, bool nonceMatchesStored);

// Whether the identity fields on a connection attempt are the width they must be.
bool IsWellFormedHello(size_t nonceLength, size_t ephemeralLength);

// What can be done with a proof, given the slot's state and the widths that arrived.
enum ProofVerdict
{
	// Everything is present, so go and check the signature.
	PROOF_JUDGE,

	// A field is the wrong width, which is a truncated or tampered packet rather than a wrong key.
	PROOF_MALFORMED,

	// Nothing was ever issued to this slot, so there is no message this could be a signature over.
	PROOF_NOCHALLENGE,
};

ProofVerdict ProofReadiness(bool hasStoredChallenge, size_t accountKeyLength, size_t signatureLength);

} // namespace zx

#endif // ZX_CONNECTCHALLENGE_COMPUTE_H
