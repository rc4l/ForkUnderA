// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Everything dynamic resolution decides, with no clock, no GPU and no engine: the frame time it aims under, the scales it steps between, when it steps, and the scene's pixels at a scale.

#ifndef ZX_DYNRES_COMPUTE_H
#define ZX_DYNRES_COMPUTE_H

namespace zx { namespace dynres {

// [rc4l] The caps a frame's time is held under.
struct DynresCaps
{
	double refreshHz = 0.0;   // [rc4l] the display's, 0 when unknown
	bool vsync = false;
	int limitFps = 0;         // [rc4l] features/frame-limit's, 0 for none
	int targetFps = 0;        // [rc4l] vid_fua_dynres_target, 0 for the display's refresh
};

// [rc4l] The frame time to aim under, in ms: the target or else the display's refresh (60 Hz when unknown), never above vsync's refresh or the frame limit, since a cap a scale cannot beat is no reason to blur.
double ComputeFrameBudgetMs(const DynresCaps &caps);

// [rc4l] The most steps any step list holds.
enum { kMaxDynresSteps = 8 };

// [rc4l] The scales to step between into out, full first, none under minScale: eighths when upscaled linearly, and 3/4 and halves when by point, whose uneven pixels show least at those; returns how many.
int ComputeDynresSteps(float minScale, bool pointFilter, float out[kMaxDynresSteps]);

// [rc4l] One frame as the controller sees it.
struct DynresSample
{
	double gpuMs = -1.0;      // [rc4l] the GPU's time for a frame, from its timestamp queries, below 0 when unknown
	double cpuMs = 0.0;       // [rc4l] the frame's own work on the CPU, every wait to present excluded
	double frameMs = 0.0;     // [rc4l] the frame's whole length, its waits included
};

// [rc4l] How the controller reacts.
struct DynresTuning
{
	double downAt = 0.85;     // [rc4l] step down once the GPU's average passes this share of the budget, leaving vsync a frame's worth of slack
	double upAt = 0.7;        // [rc4l] step up once the next step's predicted cost fits under this share
	double weight = 0.2;      // [rc4l] how far one sample moves the average
	int settleFrames = 8;     // [rc4l] samples ignored after a step, while the GPU's timings still describe the old scale
	int upFrames = 30;        // [rc4l] frames the headroom must hold before a step up
	int upFramesUntimed = 120;// [rc4l] frames on budget before a step up where the GPU is not timed, since its headroom is then a guess
	int untimedMisses = 3;    // [rc4l] frames in a row that waited on the GPU before an untimed step down
	double lateAt = 1.5;      // [rc4l] an untimed frame longer than this many budgets missed a refresh, since one paced to a display is late by a whole refresh or not at all
	int trialFrames = 30;     // [rc4l] frames an untimed step down is judged over once settled
	double trialGain = 0.9;   // [rc4l] the share of its old length a frame must come under for an untimed step down to be kept, unless no longer late
	double refuseMs = 1000.0; // [rc4l] half the first wait before another untimed step down once one is undone, in time since slow frames are what it waits through
};

// [rc4l] Where the controller stands.
struct DynresState
{
	int step = 0;             // [rc4l] into the step list, 0 for full scale
	int settle = 0;
	int roomFrames = 0;
	double gpuAvgMs = 0.0;
	bool haveAvg = false;
	int changes = 0;          // [rc4l] steps taken, for telemetry
	int missStreak = 0;       // [rc4l] untimed frames in a row that waited on the GPU
	int sinceUp = 1 << 20;    // [rc4l] frames since the last step up
	int upWait = 0;           // [rc4l] the untimed wait before a step up, doubled each time one is soon undone
	double frameAvgMs = 0.0;  // [rc4l] the untimed frames' average since the last step, 0 for none yet
	double beforeMs = 0.0;    // [rc4l] that average just before the last untimed step down
	int trial = 0;            // [rc4l] frames left judging the last untimed step down
	double downWaitMs = 0.0;  // [rc4l] the untimed wait before a step down once one is undone, doubled each time
	double refuseMs = 0.0;    // [rc4l] what is left of that wait
};

// [rc4l] Whether an untimed frame waited on the GPU: late (t.lateAt) while the CPU worked well under the frame, the only sign of the GPU a browser gives.
bool ComputeFrameWaitedOnGpu(const DynresSample &x, double budgetMs, const DynresTuning &t);

// [rc4l] The state after a frame: a step down when the GPU is over budget and is what holds the frame, up once the next step's predicted cost has fitted for long enough, else as it was; untimed, only frames that waited on the GPU step down, and only a step down that shortens them is kept.
DynresState ComputeDynresStep(const DynresState &s, const DynresSample &x, double budgetMs, const float *steps, int count, const DynresTuning &t);

// [rc4l] The low tenth of n frame intervals in ms, a browser's refresh period when its frames keep up, which one early frame cannot pull down as the shortest would; 0 for none.
double ComputeShortIntervalMs(const double *ms, int n);

// [rc4l] The scene's pixels in a frame of frameW by frameH at scale, at least one each way and the whole frame from full scale up.
struct SceneRect { int w = 0, h = 0; };
SceneRect ComputeSceneRect(int frameW, int frameH, float scale);

// [rc4l] A display's refresh from a browser's short frame interval, snapped to the common rate within 4% of it; 0 above 1000 Hz, which is a browser drawing for no display.
double ComputeRefreshFromIntervalMs(double shortestMs);

// [rc4l] How many frame intervals a browser's refresh is read from, and how many such windows it is held over.
enum { kRefreshFrames = 120, kRefreshWindows = 8 };

// [rc4l] A browser's recent frame intervals and the refresh each of its last whole windows read.
struct FrameRefresh
{
	double intervals[kRefreshFrames] = {};
	int count = 0, next = 0, sinceWindow = 0;
	double windowHz[kRefreshWindows] = {};
	int windows = 0, nextWindow = 0;
};

// [rc4l] Adds a frame's interval in ms (none at 0 or below) and returns a browser's refresh in Hz, 0 until known: its last intervals' low tenth, held at the fastest of its last whole windows so a run of slow frames is not taken for a slow display.
double ComputeFrameRefreshHz(FrameRefresh &r, double frameMs);

}} // namespace zx::dynres

#endif // ZX_DYNRES_COMPUTE_H
