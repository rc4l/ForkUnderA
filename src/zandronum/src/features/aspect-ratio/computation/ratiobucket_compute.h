// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The aspect-ratio buckets the video mode lists and vid_aspect still name: 0 4:3, 1 16:9, 2 16:10, 3 17:10, 4 5:4, 6 21:9.

#ifndef ZX_RATIOBUCKET_COMPUTE_H
#define ZX_RATIOBUCKET_COMPUTE_H

namespace zx { namespace aspect {

// [rc4l] The bucket a width and height fall in, 5:4 only under vid_tft; 21:9 is zandronum@bab77c3aab00b84c18fc1fb997af3ef2776d8f07.
int ComputeRatioBucket(int width, int height, bool tft);

// [rc4l] The bucket vid_aspect (1 to 6) or vid_nowidescreen forces, or -1 for none.
int ComputeForcedRatioBucket(int vidAspect, bool noWidescreen, bool tft, int width, int height);

// [rc4l] Whether a bucket is wider than 4:3, which vid_listmodes shows in gold.
bool ComputeRatioIsWidescreen(int bucket);

}} // namespace zx::aspect

#endif // ZX_RATIOBUCKET_COMPUTE_H
