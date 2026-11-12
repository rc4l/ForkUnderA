// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] The TID hash's bucket.
#include "features/tid-hash/computation/tidhash_compute.h"

static_assert((kZxTidHashSize & (kZxTidHashSize - 1)) == 0, "the TID hash's size must be a power of two");

int ComputeTidBucket(int tid)
{
	return (int)((unsigned)tid & (unsigned)(kZxTidHashSize - 1));
}
