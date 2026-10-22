// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The GPU objects the backend made while a level was being played rather than while it loaded, each a compile or upload a frame waited on.

#ifndef ZX_MADEINPLAY_COMPUTE_H
#define ZX_MADEINPLAY_COMPUTE_H

#include <string>
#include <vector>

namespace zx { namespace hwrender {

// [rc4l] Frames a ready level draws before what is made counts as made in play, since the first frames finish its set-up behind the wipe.
const int kMadeInPlayArmFrames = 2;

// [rc4l] The most kinds and names kept apart, so a level making textures without end stays a bounded report.
const size_t kMadeInPlayMaxEntries = 256;

struct MadeInPlayEntry
{
	std::string kind;   // [rc4l] shader, pipeline, texture or buffer
	std::string name;   // [rc4l] the object's debug name
	int count;          // [rc4l] how many were made under it
	int firstFrame;     // [rc4l] the frame of the level it was first made in
};

struct MadeInPlay
{
	int readyFrames = 0;   // [rc4l] frames drawn since the level was ready
	int total = 0;         // [rc4l] every object made in play, kept apart or not
	std::vector<MadeInPlayEntry> entries;
};

// [rc4l] A frame drawn with the level ready or not; a level not ready starts the count again.
void ComputeMadeInPlayFrame(MadeInPlay &m, bool ready);

// [rc4l] Whether an object made now is made in play, recording it under its kind and name when it is.
bool ComputeMadeInPlayNote(MadeInPlay &m, const char *kind, const char *name);

// [rc4l] A new level forgets the last one's.
void ComputeMadeInPlayClear(MadeInPlay &m);

// [rc4l] The report fua_dg_made prints: the total, then each kind and name with its count and first frame.
std::string ComputeMadeInPlayReport(const MadeInPlay &m);

}} // namespace zx::hwrender

#endif // ZX_MADEINPLAY_COMPUTE_H
