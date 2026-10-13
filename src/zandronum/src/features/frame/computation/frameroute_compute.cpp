// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/frameroute_compute.h"

namespace zx { namespace frame {

FrameRoute ComputeFrameRoute(const FrameRouteIn &in)
{
	FrameRoute r;
	r.backendDraws = in.hasBackend && in.backendOn;
	const bool covered = in.backendPresenting && in.sceneUp;
	const bool glWanted = !in.standalone || !in.embedded || in.checking;
	r.glDraws = in.hasGL && (!covered || glWanted);
	r.savePicFromBackend = !in.hasGL || (r.backendDraws && in.sceneUp);
	return r;
}

}} // namespace zx::frame
