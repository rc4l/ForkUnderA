// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] When the tic stream of sprites the shader moves must be built again: everything its build read that a frame can change.

#ifndef ZX_TICSTREAM_COMPUTE_H
#define ZX_TICSTREAM_COMPUTE_H

#include <cstdint>

namespace zx { namespace frame {

// [Doruk-Aksoy] What a tic stream was built from: the render copy's fill, the cvar sets so far, the actor the view is from and the fixed colormap.
struct TicStreamStamp
{
	unsigned fill;
	unsigned cvarSets;
	uintptr_t viewActor;
	unsigned fixedColormap;
};

// [Doruk-Aksoy] Whether a stream built at `built` is stale now: none is live, a tic was filled, or a setting, the view's actor or the fixed colormap changed between tics, as a paused game's menu does.
bool ComputeTicStreamStale(bool live, const TicStreamStamp &built, const TicStreamStamp &now);

}} // namespace zx::frame

#endif // ZX_TICSTREAM_COMPUTE_H
