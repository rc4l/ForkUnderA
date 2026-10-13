// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which renderer draws a frame and whose picture is shown, decided in one place for the frame driver and GL's frame buffer.

#ifndef ZX_FRAMEROUTE_COMPUTE_H
#define ZX_FRAMEROUTE_COMPUTE_H

namespace zx { namespace frame {

struct FrameRouteIn
{
	bool hasGL, hasBackend;   // [rc4l] which renderers the launch has
	bool backendOn;           // [rc4l] the backend draws frames: always without GL, else while fua_vulkan is on
	bool sceneUp;             // [rc4l] the backend's scene is set up and drawable
	bool backendPresenting;   // [rc4l] the backend's window is embedded over GL's with a swap chain
	bool standalone;          // [rc4l] fua_dg_standalone
	bool embedded;            // [rc4l] fua_dg_embed
	bool checking;            // [rc4l] any check against GL is on
};

struct FrameRoute
{
	bool backendDraws;         // [rc4l] the backend draws this frame
	bool glDraws;              // [rc4l] GL draws this frame, which is exactly when its window is shown
	bool savePicFromBackend;   // [rc4l] the picture of what the player sees is the backend's
};

// [rc4l] GL draws unless the backend's scene covers its window and its own picture is not wanted (standalone on, embedded, no check).
FrameRoute ComputeFrameRoute(const FrameRouteIn &in);

// [rc4l] What a renderer the frame driver holds is, which decides on which frames it draws.
enum RendererKind { kRendererGL, kRendererBackend };

// [rc4l] Whether a renderer of `kind` draws a frame on `route`.
bool ComputeKindDraws(const FrameRoute &route, int kind);

// [rc4l] The kind whose picture a save takes on `route`.
int ComputeSavePicKind(const FrameRoute &route);

}} // namespace zx::frame

#endif // ZX_FRAMEROUTE_COMPUTE_H
