// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Whether the view the player sees turns every frame or only when a game tic runs, judged from the angle each frame was drawn with.

#ifndef ZX_VIEWTRACE_COMPUTE_H
#define ZX_VIEWTRACE_COMPUTE_H

namespace zx { namespace frame {

// [rc4l] One frame's main view: when it was set up, the angle it was drawn with, the mouse turn not yet in a tic, and the tic it belongs to.
struct ViewTraceSample
{
	long long us;
	long long presentAt;   // [rc4l] when the frame before this one began presenting, on the same clock as `us`, 0 for not at all
	float phases[8];       // [rc4l] the frame before this one's microseconds per phase (features/framephase)
	unsigned angle;        // [rc4l] viewangle, BAM
	unsigned localAngle;   // [rc4l] LocalViewAngle, BAM
	int gametic;
};

struct ViewTraceSummary
{
	int frames;
	double spanMs;
	int ticFrames;           // [rc4l] frames a new tic had run before
	int turnedFrames;        // [rc4l] frames drawn at a different angle from the one before
	int mouseOnlyFrames;     // [rc4l] frames whose pending mouse turn moved within a tic while the drawn angle did not
	double turnDeg;          // [rc4l] all the turning, in degrees
	double ticTurnDeg;       // [rc4l] of it, the turning on tic frames
	double maxStepDeg;       // [rc4l] the largest turn between two frames
	double maxStillMs;       // [rc4l] the longest gap between two turning frames
	int longGaps;            // [rc4l] gaps of kLongGapMs or more between two turning frames
	double stepAfterGapDeg;  // [rc4l] the mean turn of the frame that ends such a gap, which is large when input arrived late in one lump
	double meanStepDeg;      // [rc4l] the mean turn of a turning frame
};

// [rc4l] A gap between turning frames this long is a stall a player can see, at any frame rate.
const double kLongGapMs = 20.0;

// [rc4l] The summary of `n` frames in order; a view that turns between tics spreads its turning over every frame, one that turns only at tics puts it all on the tic frames.
ViewTraceSummary ComputeViewTraceSummary(const ViewTraceSample *s, int n);

// [rc4l] The turn from angle `from` to `to` in degrees, the short way round.
double ComputeTurnDegrees(unsigned from, unsigned to);

}} // namespace zx::frame

#endif // ZX_VIEWTRACE_COMPUTE_H
