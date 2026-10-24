// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The bytes each GPU buffer was sent, per frame, for telemetry's gpu/upload/ breakdown: a browser carries every one through its GPU process.

#ifndef ZX_UPLOADTALLY_COMPUTE_H
#define ZX_UPLOADTALLY_COMPUTE_H

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace zx { namespace hwrender {

struct UploadTally
{
	std::map<std::string, double> bytes;   // [rc4l] by buffer name, since the last take
	int frames = 0;                        // [rc4l] frames since the last take
};

// [rc4l] Adds bytes sent to the buffer named name, "unnamed" for none.
void ComputeUploadTallyAdd(UploadTally &t, const char *name, double bytes);

// [rc4l] Counts a frame.
void ComputeUploadTallyFrame(UploadTally &t);

// [rc4l] Each buffer's bytes per frame since the last take, most first, and starts the tally over; per frame over one frame when none was counted.
std::vector<std::pair<std::string, double>> ComputeUploadTallyTake(UploadTally &t);

}} // namespace zx::hwrender

#endif // ZX_UPLOADTALLY_COMPUTE_H
