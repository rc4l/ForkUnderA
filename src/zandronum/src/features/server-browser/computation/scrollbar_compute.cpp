// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/scrollbar_compute.h"

namespace zx
{

int ComputeThumbHeight(int trackHeight, int visible, int total, int minThumb)
{
	if (trackHeight <= 0)
		return 0;
	if ((total <= 0) || (visible <= 0) || (total <= visible))
		return trackHeight;					// everything fits; a full thumb says so

	int thumb = (trackHeight * visible) / total;

	if (thumb < minThumb)
		thumb = minThumb;
	if (thumb > trackHeight)
		thumb = trackHeight;

	return thumb;
}

int ComputeThumbTop(int trackHeight, int thumbHeight, int first, int maxFirst)
{
	// The thumb travels over what is left of the track after its own body. Dividing by the track
	// height instead is wrong by exactly one thumb at the bottom.
	const int travel = trackHeight - thumbHeight;
	if ((travel <= 0) || (maxFirst <= 0))
		return 0;

	int clamped = first;
	if (clamped < 0)
		clamped = 0;
	if (clamped > maxFirst)
		clamped = maxFirst;

	return (travel * clamped) / maxFirst;
}

int ComputeFirstFromPointer(int pointerOffset, int trackHeight, int thumbHeight, int maxFirst)
{
	if (maxFirst <= 0)
		return 0;

	const int travel = trackHeight - thumbHeight;
	if (travel <= 0)
		return 0;						// the thumb fills the track; there is nowhere to go

	// Centre the thumb on the pointer rather than starting it there, which is what grabbing a
	// scrollbar does everywhere else. Without this every click undershoots by half a thumb.
	int top = pointerOffset - (thumbHeight / 2);
	if (top < 0)
		top = 0;
	if (top > travel)
		top = travel;

	// Rounded rather than truncated: at the very bottom of the track a truncating divide lands one
	// row short of the end, so the last row can never be reached by dragging.
	return ((top * maxFirst) + (travel / 2)) / travel;
}

} // namespace zx
