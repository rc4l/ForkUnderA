// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] The bucket an actor's TID hashes to, in a table wide enough that a mod tagging thousands of monsters keeps its chains short.
#ifndef ZX_TIDHASH_COMPUTE_H
#define ZX_TIDHASH_COMPUTE_H

// [Doruk-Aksoy] Buckets in AActor's TID hash, a power of two wide enough for a mod numbering 12,800 monsters in one run (upstream keeps 128).
constexpr int kZxTidHashSize = 16384;

// [Doruk-Aksoy] The bucket for a TID, every TID (negative ones too) landing in range and one TID always in the same bucket.
int ComputeTidBucket(int tid);

#endif
