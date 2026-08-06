// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wad-serve/computation/ratebucket_compute.h"

namespace zx
{

// 1 TB. Any real send size mins against this without overflowing, and it is obvious on sight that it
// is a sentinel rather than a configured cap.
const long long kUnlimitedBytes = 1LL << 40;

// A quarter second. Long enough that a throttled transfer is not spinning, short enough that the
// worker notices a shutdown request promptly.
const int kMaxWaitMs = 250;

long long BucketCapacity(const RateLimit &limit)
{
	if (limit.bytesPerSec <= 0)
		return kUnlimitedBytes;
	if (limit.burstBytes > 0)
		return limit.burstBytes;
	return limit.bytesPerSec;
}

long long BucketAvailable(RateBucket &bucket, const RateLimit &limit, long long nowMs)
{
	if (limit.bytesPerSec <= 0)
		return kUnlimitedBytes;

	const double capacity = static_cast<double>(BucketCapacity(limit));

	if (!bucket.primed)
	{
		// First look at this bucket: start it full. A connection arriving on an idle server should
		// not wait for allowance nobody was spending.
		bucket.primed = true;
		bucket.lastMs = nowMs;
		bucket.tokens = capacity;
	}
	else if (nowMs > bucket.lastMs)
	{
		const double elapsedSec = static_cast<double>(nowMs - bucket.lastMs) / 1000.0;
		bucket.tokens += elapsedSec * static_cast<double>(limit.bytesPerSec);
		if (bucket.tokens > capacity)
			bucket.tokens = capacity;
		bucket.lastMs = nowMs;
	}
	else if (nowMs < bucket.lastMs)
	{
		// The clock moved backwards. Resynchronise to it rather than granting nothing until the old
		// stamp comes round again, which on a large jump would stall the transfer indefinitely.
		bucket.lastMs = nowMs;
	}

	return static_cast<long long>(bucket.tokens);
}

void BucketCharge(RateBucket &bucket, const RateLimit &limit, long long bytes)
{
	if (limit.bytesPerSec <= 0)
		return;
	if (bytes <= 0)
		return;

	bucket.tokens -= static_cast<double>(bytes);

	// Clamped rather than allowed to go negative: a caller that overcharges should cost itself one
	// refill interval, not bank a debt that keeps the connection silent for hours.
	if (bucket.tokens < 0.0)
		bucket.tokens = 0.0;
}

long long BucketTake(RateBucket &bucket, const RateLimit &limit, long long wanted, long long nowMs)
{
	if (wanted <= 0)
		return 0;

	const long long available = BucketAvailable(bucket, limit, nowMs);
	const long long grant = (available < wanted) ? available : wanted;
	BucketCharge(bucket, limit, grant);
	return grant;
}

long long BucketTakePair(RateBucket &globalBucket, const RateLimit &globalLimit,
	RateBucket &connBucket, const RateLimit &connLimit, long long wanted, long long nowMs)
{
	if (wanted <= 0)
		return 0;

	// Both availabilities first, then the smaller, then charge each. Charging as we go would spend
	// the global budget on bytes the connection cap refuses -- throttling every other downloader to
	// pay for data that was never sent.
	const long long globalAvail = BucketAvailable(globalBucket, globalLimit, nowMs);
	const long long connAvail = BucketAvailable(connBucket, connLimit, nowMs);

	long long grant = wanted;
	if (globalAvail < grant)
		grant = globalAvail;
	if (connAvail < grant)
		grant = connAvail;

	BucketCharge(globalBucket, globalLimit, grant);
	BucketCharge(connBucket, connLimit, grant);
	return grant;
}

int BucketWaitMs(const RateBucket &bucket, const RateLimit &limit, long long wanted)
{
	if (limit.bytesPerSec <= 0)
		return 0;
	if (wanted <= 0)
		return 0;

	const double deficit = static_cast<double>(wanted) - bucket.tokens;
	if (deficit <= 0.0)
		return 0;

	const double ms = (deficit * 1000.0) / static_cast<double>(limit.bytesPerSec);
	if (ms >= static_cast<double>(kMaxWaitMs))
		return kMaxWaitMs;

	// Rounded up. Sleeping fractionally short just returns to a bucket that is still empty, which
	// costs a wakeup and buys nothing.
	return static_cast<int>(ms) + 1;
}

int BucketWaitMsPair(const RateBucket &globalBucket, const RateLimit &globalLimit,
	const RateBucket &connBucket, const RateLimit &connLimit, long long wanted)
{
	const int globalWait = BucketWaitMs(globalBucket, globalLimit, wanted);
	const int connWait = BucketWaitMs(connBucket, connLimit, wanted);
	return (globalWait > connWait) ? globalWait : connWait;
}

} // namespace zx
