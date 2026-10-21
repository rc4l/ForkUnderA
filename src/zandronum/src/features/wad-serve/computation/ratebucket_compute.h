// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] How many bytes a transfer may send right now -- the whole bandwidth policy, as arithmetic.
//
// Serving WADs from the game server means the download and the tic stream share one uplink. The
// download thread can never stall the game loop (it is a separate thread on a separate socket), so
// the only thing actually contended is the pipe: an uncapped transfer fills the uplink, tic packets
// queue behind file bytes, and players see it as lag. A token bucket is what keeps that from
// happening.
//
// Two limits, and the pairing between them is the point:
//
//   - a GLOBAL budget shared by every active transfer, and
//   - a PER-CONNECTION cap.
//
// The global one is the limit that matters, and per-connection alone is a trap -- it silently
// multiplies by client count, so "256 KB/s each" is 5 MB/s once twenty people join at a map change.
// ioquake3 gets this right with sv_dlRate, a server-wide budget; Source has no KB/s knob at all and
// lets transfers ride the client's own `rate`, which is a large part of why every busy Source server
// ends up on FastDL.
//
// BucketTakePair exists because charging the two buckets separately is a real bug rather than a
// tidiness point. Take 64 KB from the global budget, then discover the connection bucket only has
// 8 KB, and the 56 KB difference has been spent out of the server-wide budget without being sent to
// anyone -- every other downloader is throttled to pay for bytes that never moved. So availability is
// computed on both first, the smaller wins, and only that amount is charged to each.
//
// Time is a parameter, never read here. Same reason as every other compute unit in the tree: a rate
// limiter whose tests have to sleep to observe a refill is a rate limiter with slow, flaky tests.
// The caller passes a monotonic millisecond stamp and these functions are exact and repeatable.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_RATEBUCKET_COMPUTE_H
#define ZX_RATEBUCKET_COMPUTE_H

namespace zx
{

// What BucketAvailable reports when a limit is switched off. Large enough that a caller can min() it
// against a real byte count without overflow, and small enough to spot in a debugger.
extern const long long kUnlimitedBytes;

// The longest BucketWaitMs will ever ask a caller to sleep. A transfer thread wakes at least this
// often even when heavily throttled, so shutdown and cancellation stay responsive instead of being
// held up behind one long sleep.
extern const int kMaxWaitMs;

// A configured cap. `bytesPerSec` <= 0 means unlimited, which is how "the operator turned this knob
// off" is spelled everywhere below. `burstBytes` <= 0 means one second's worth, which is the useful
// default: it lets an idle connection start immediately at full speed without letting a bucket bank
// minutes of unused allowance and then dump it in one spike.
struct RateLimit
{
	long long bytesPerSec;
	long long burstBytes;

	RateLimit() : bytesPerSec(0), burstBytes(0) {}
	RateLimit(long long perSec, long long burst) : bytesPerSec(perSec), burstBytes(burst) {}
};

// The live state of one bucket. Starts unprimed: the first call seeds it full, so a connection that
// arrives on an idle server is not made to wait for tokens that nobody was using anyway.
struct RateBucket
{
	double tokens;
	long long lastMs;
	bool primed;

	RateBucket() : tokens(0.0), lastMs(0), primed(false) {}
};

// Ceiling on accumulated tokens for `limit` -- burstBytes, or one second of rate when unset.
long long BucketCapacity(const RateLimit &limit);

// Advance `bucket` to `nowMs` and report what may be sent. kUnlimitedBytes when the limit is off.
//
// A monotonic clock is assumed but not required: if `nowMs` goes backwards the bucket resynchronises
// to the new stamp without granting anything, rather than stalling until the old one is reached
// again. Worth handling rather than asserting -- this runs for months on other people's machines.
long long BucketAvailable(RateBucket &bucket, const RateLimit &limit, long long nowMs);

// Spend `bytes` from `bucket`. A no-op when the limit is off. Never drives tokens below zero, so a
// caller that charges more than BucketAvailable offered costs itself one refill, not an unbounded
// debt that stalls the connection for hours.
void BucketCharge(RateBucket &bucket, const RateLimit &limit, long long bytes);

// Availability and charge in one step, against a single bucket: the amount that may be sent now,
// between 0 and `wanted`.
long long BucketTake(RateBucket &bucket, const RateLimit &limit, long long wanted, long long nowMs);

// The one the transfer loop actually calls. Grants against a global budget and a per-connection cap
// together, charging each only for what both allow.
long long BucketTakePair(RateBucket &globalBucket, const RateLimit &globalLimit,
	RateBucket &connBucket, const RateLimit &connLimit, long long wanted, long long nowMs);

// How long to sleep before `wanted` bytes could be granted, given what `bucket` holds now. 0 when it
// could be granted immediately, capped at kMaxWaitMs. Does not refill -- call it after
// BucketAvailable has already advanced the bucket and come up short.
int BucketWaitMs(const RateBucket &bucket, const RateLimit &limit, long long wanted);

// The sleep for a pair: the longer of the two waits, since both have to be satisfiable before the
// send can happen.
int BucketWaitMsPair(const RateBucket &globalBucket, const RateLimit &globalLimit,
	const RateBucket &connBucket, const RateLimit &connLimit, long long wanted);

} // namespace zx

#endif // ZX_RATEBUCKET_COMPUTE_H
