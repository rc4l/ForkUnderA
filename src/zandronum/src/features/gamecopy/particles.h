// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The render copy's particles, taken between tics with each subsector's list, since a tic moves, spawns and expires particles while a frame draws.

#ifndef ZX_GAMECOPY_PARTICLES_H
#define ZX_GAMECOPY_PARTICLES_H

#include "actor.h"      // [rc4l] what p_effect.h names without including
#include "p_effect.h"

namespace zx { namespace gamecopy {

// [rc4l] The first copied particle in a subsector, NO_PARTICLE for none; the next is CopiedParticle(i)->snext, as P_FindParticleSubsectors links the live ones.
WORD FirstParticleIn(int subsector);
// [rc4l] Copied particle i.
particle_t *CopiedParticle(WORD i);
// [rc4l] The live particle copied particle i was taken from, named and never read, as GL's sprite check names it.
const void *ParticleIdentity(WORD i);

// [rc4l] Takes the active particles and each subsector's list; part of the fill.
void FillParticles();

}} // namespace zx::gamecopy

#endif // ZX_GAMECOPY_PARTICLES_H
