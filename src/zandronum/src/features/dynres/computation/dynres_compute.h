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
	bool untimed = false;     // [rc4l] the GPU's frames are not timed, as in a browser
};

// [rc4l] The frame rate an untimed controller aims at unless a target is picked, and the slowest refresh read from frames it believes, since every step it takes is a trial and slow frames read as a slow display.
const double kUntimedHz = 60.0;

// [rc4l] The frame time to aim under, in ms: the target or else the display's refresh (60 Hz when unknown or untimed), never above vsync's refresh or the frame limit, since a cap a scale cannot beat is no reason to blur.
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
	double windowMs = 1000.0; // [rc4l] untimed, frames are judged by their mean over windows this long, since Firefox lets several through and then holds one for the GPU
	double slowAt = 1.2;      // [rc4l] an untimed window whose mean passes this share of the budget is slow
	double roomAt = 0.8;      // [rc4l] an untimed window under this share of the budget has room for a step up
	int roomWindows = 2;      // [rc4l] such windows in a row before a step up
	double upWaitMs = 3000.0; // [rc4l] time below full scale before an untimed step up is tried, doubled each time one is soon undone
	double keepAt = 0.95;     // [rc4l] an untimed step down is kept once its window's mean is under this share of the one before it, since one kept for nothing is soon undone by a step up
	double trialGain = 0.9;   // [rc4l] an untimed step up is undone once its slow window grows past the one before over this share
	double settleMs = 250.0;  // [rc4l] untimed time after a step, on top of settleFrames, before its window counts, while frames the browser still holds were drawn at the old scale
	double refuseMs = 1000.0; // [rc4l] half the first wait before another untimed step down once one is undone
	double rearmAt = 1.25;    // [rc4l] frames this much heavier than when a step down was undone end the wait, since the load has changed
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
	double quietMs = 0.0;     // [rc4l] untimed time left before the window after a step counts
	double winMs = 0.0;       // [rc4l] the untimed window's frames so far, in time
	int winFrames = 0;
	double lastWindowMs = 0.0;// [rc4l] the last whole untimed window's mean frame, 0 for none yet
	bool trial = false;       // [rc4l] whether the window under way judges the last untimed step
	bool trialUp = false;     // [rc4l] whether the step judged was one up
	double beforeMs = 0.0;    // [rc4l] the mean of the window before the last untimed step
	double sinceUpMs = 1e9;   // [rc4l] time since the last step up
	double roomMs = 0.0;      // [rc4l] untimed time below full scale since the last step kept or tried up
	int roomRun = 0;          // [rc4l] untimed windows in a row with room for a step up
	double upWaitMs = 0.0;    // [rc4l] the untimed wait before a step up, doubled each time one is soon undone
	double upFailMs = 0.0;    // [rc4l] the frames before the last untimed step up undone, whose doubled wait frames lighter than them forget
	double downWaitMs = 0.0;  // [rc4l] the untimed wait before a step down once one is undone, doubled each time
	double refuseMs = 0.0;    // [rc4l] what is left of that wait
	double refusedAtMs = 0.0; // [rc4l] the frames when the last untimed step down was undone
};

// [rc4l] The state after a frame: a step down when the GPU is over budget and is what holds the frame, up once the next step's predicted cost has fitted for long enough, else as it was; untimed, a slow window of frames steps down and a wait or room steps up, and each step is kept only if the next window bears it out.
DynresState ComputeDynresStep(const DynresState &s, const DynresSample &x, double budgetMs, const float *steps, int count, const DynresTuning &t);

// [rc4l] Why the controller moved from was to now, for fua_dynres_log; "" when it did not.
const char *ComputeDynresWhy(const DynresState &was, const DynresState &now, bool timed);

// [rc4l] Whether the controller may read a frame: a timed one always, and an untimed one only while the game runs (no pause, menu or console) in a focused window, since a paused game draws once a tic and a browser slows a page out of focus.
bool ComputeFrameJudged(bool timed, bool running, bool focused);

// [rc4l] The state over a frame the controller may not read: the scale holds, the runs it was counting start over, and settleFrames frames back are let pass.
DynresState ComputeDynresUnjudged(const DynresState &s, int settleFrames);

// [rc4l] The median of n frame intervals in ms, a browser's refresh period while most of its frames keep up, which neither one early frame nor a display's jitter pulls short; 0 for none.
double ComputeMedianIntervalMs(const double *ms, int n);

// [rc4l] The scene's pixels in a frame of frameW by frameH at scale, at least one each way and the whole frame from full scale up.
struct SceneRect { int w = 0, h = 0; };
SceneRect ComputeSceneRect(int frameW, int frameH, float scale);

// [rc4l] A display's refresh from a browser's short frame interval, snapped to the common rate within 4% of it; 0 above 1000 Hz, which is a browser drawing for no display.
double ComputeRefreshFromIntervalMs(double shortestMs);

// [rc4l] How many frame intervals a browser's refresh is read from.
enum { kRefreshFrames = 120 };

// [rc4l] A browser's recent frame intervals and the fastest refresh a whole window of them read.
struct FrameRefresh
{
	double intervals[kRefreshFrames] = {};
	int count = 0, next = 0, sinceWindow = 0;
	double fastestHz = 0.0;
};

// [rc4l] Adds a frame's interval in ms (none at 0 or below) and returns a browser's refresh in Hz, 0 until known: its last intervals' median, held at the fastest whole window yet, since no run of slow frames, however long, is a slower display.
double ComputeFrameRefreshHz(FrameRefresh &r, double frameMs);

}} // namespace zx::dynres

#endif // ZX_DYNRES_COMPUTE_H
