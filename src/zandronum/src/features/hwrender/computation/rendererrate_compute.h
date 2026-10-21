// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] vid_fps per renderer: the frames each drew in the last whole second, and how long its own part of a frame took, which is what tells GL and the backend apart in one loop.

#ifndef ZX_RENDERERRATE_COMPUTE_H
#define ZX_RENDERERRATE_COMPUTE_H

#include <stddef.h>

namespace zx { namespace hwrender {

// [rc4l] One renderer's frames in the second under way, and what its last whole second averaged.
struct RendererRate
{
	unsigned second;
	int frames;
	double busyMs;
	int shownMs, shownFps;
	unsigned lastMs;
	bool seen;
};

// [rc4l] A rate with nothing counted yet.
RendererRate ComputeRateStart();

// [rc4l] A frame that took frameMs, ending at nowMs; a new second makes the one before it what is shown: its average ms and the frames it drew, never the frames that time would allow, which read 1000 where a browser showed 480.
void ComputeRateFrame(RendererRate &r, unsigned nowMs, double frameMs);

// [rc4l] Whether the renderer drew a frame within the last second, which is when its window is up to show a rate.
bool ComputeRateLive(const RendererRate &r, unsigned nowMs);

// [rc4l] The backend's name on its vid_fps line, from its device's API (BackendApiName): "WebGPU" on WebGPU, else "VK".
const char *ComputeBackendRateName(const char *apiName);

// [rc4l] The vid_fps line for one renderer, "GL  3 ms (310 fps)", into out; returns its length.
int ComputeRateText(char *out, size_t size, const char *name, int ms, int fps);

// [rc4l] Where vid_fps draws `chars` 8x8 glyphs at the screen's top right, `scale` times larger (fua_fps_scale): the canvas the text is drawn on, its x there, and the black box behind it in screen pixels.
struct RateBox
{
	int scale;                          // [rc4l] the scale used, at least 1 and no larger than fits the screen
	int virtualWidth, virtualHeight;    // [rc4l] the canvas the text is drawn on, `scale` times smaller than the screen
	int x;                              // [rc4l] the text's left edge on that canvas
	int boxLeft, boxHeight;             // [rc4l] the box behind it, in screen pixels, out to the right edge
};
RateBox ComputeRateBox(int screenWidth, int screenHeight, int chars, int scale);

}} // namespace zx::hwrender

#endif
