// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
//
// [rc4l] What hitboxviz.cpp and GL's half of the overlay (hitboxviz_gl.cpp) share: the offline gate, one actor's boxes and the live blasts.

#ifndef ZX_HITBOXVIZ_INTERNAL_H
#define ZX_HITBOXVIZ_INTERNAL_H

#include "tarray.h"
#include "features/hitboxviz/computation/boxedges_compute.h"   // [rc4l] Vertex3

class AActor;

namespace zx { namespace hitboxviz {

// "Nobody else is in this game": not a client of someone's server, and not a server ourselves.
bool IsOfflineGame();

// [rc4l] One actor's boxes into `physical` and `attack`, viewActor being the one the camera is attached to.
void AppendActor(AActor *thing, AActor *viewActor, TArray<Vertex3> &physical, TArray<Vertex3> &attack);

// [rc4l] The blasts still alive at this tic as wireframe into `dest`, when explosions are shown at all.
void CollectBlasts(TArray<Vertex3> &dest);

}} // namespace zx::hitboxviz

#endif // ZX_HITBOXVIZ_INTERNAL_H
