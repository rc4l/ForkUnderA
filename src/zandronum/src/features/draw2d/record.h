// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The frame's 2D layer as a list of screen-space quads in draw order, recorded through Record2DCanvas for every renderer and cleared at the top of each frame.

#ifndef ZX_DRAW2D_RECORD_H
#define ZX_DRAW2D_RECORD_H

#include "features/draw2d/computation/quad2d_compute.h"   // [rc4l] Quad2D

namespace zx { namespace draw2d {

// [rc4l] A 2D draw for both windows, GL's alone (the backend skips it) or the backend's alone (GL draws it with colour writes off).
enum { k2DBoth = 0, k2DGLOnly = 1, k2DBackendOnly = 2 };
void Set2DTarget(int target);
int  Get2DTarget();

// [rc4l] Drop last frame's list, once at the top of D_Display before anything draws.
void Clear2D();

// [rc4l] Record one quad, allocation-free once the list has settled at its high-water mark.
void Record2D(const Quad2D &q);

// [rc4l] This frame's quads in submission order, which is the draw order, since 2D is painted back to front.
const Quad2D *Quads2D(int &count);

// [rc4l] Bumped by every Clear2D, so a renderer builds its vertices once per frame rather than once per draw.
unsigned int Generation2D();

// [rc4l] The screen the coordinates are relative to, captured when the frame's first quad is recorded.
void GetScreen2D(int &w, int &h);

}} // namespace zx::draw2d

#endif // ZX_DRAW2D_RECORD_H
