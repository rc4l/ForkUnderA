// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What a buffer was last sent, so a frame writing it the same bytes again sends nothing: the light grid and the 2D layer of a still view.

#ifndef ZX_UPLOADGUARD_COMPUTE_H
#define ZX_UPLOADGUARD_COMPUTE_H

#include <cstddef>
#include <vector>

namespace zx { namespace hwrender {

struct UploadShadow
{
	long long object = -1;               // [rc4l] the buffer's unique id, which a buffer made again does not keep
	std::vector<unsigned char> bytes;    // [rc4l] what it holds from the last upload through this shadow
};

// [rc4l] Whether data differs from what buffer `object` holds, keeping it as the buffer's contents when it does; a different buffer always differs.
bool ComputeUploadChanged(UploadShadow &s, long long object, const void *data, size_t bytes);

// [rc4l] A run of entries to send, the first and how many.
struct UploadRun { unsigned first, count; };

// [rc4l] The runs of entries (stride words each, `entries` of them) where now differs from was, two runs fewer than bridge unchanged entries apart joined, so a handful of writes carries every change.
std::vector<UploadRun> ComputeChangedRuns(const unsigned *was, const unsigned *now, unsigned entries, unsigned stride, unsigned bridge);

}} // namespace zx::hwrender

#endif // ZX_UPLOADGUARD_COMPUTE_H
