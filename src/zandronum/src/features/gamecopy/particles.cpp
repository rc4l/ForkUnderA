// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/particles.h"
#include "features/telemetry/telemetry.h"

#include "c_cvars.h"
#include "r_state.h"
#include "r_utility.h"

#include <vector>

EXTERN_CVAR(Bool, r_particles)

namespace zx { namespace gamecopy {

namespace {

std::vector<particle_t> g_particles;
std::vector<WORD> g_liveIndex;   // [rc4l] which live particle each copy is
std::vector<WORD> g_firstIn;     // [rc4l] each subsector's first copy

} // namespace

WORD FirstParticleIn(int subsector)
{
	return subsector >= 0 && subsector < (int)g_firstIn.size() ? g_firstIn[subsector] : NO_PARTICLE;
}

particle_t *CopiedParticle(WORD i)
{
	return &g_particles[i];
}

const void *ParticleIdentity(WORD i)
{
	return &Particles[g_liveIndex[i]];
}

void FillParticles()
{
	ZX_ZONE("copy/particle_fill");   // [rc4l] the particles and their subsector lists taken
	g_particles.clear();
	g_liveIndex.clear();
	g_firstIn.assign(numsubsectors, NO_PARTICLE);
	if (!r_particles) return;
	// [rc4l] In the live list's order, each pushed on its subsector's list, as P_FindParticleSubsectors does.
	for (WORD i = ActiveParticles; i != NO_PARTICLE; i = Particles[i].tnext)
	{
		const WORD k = (WORD)g_particles.size();
		g_particles.push_back(Particles[i]);
		g_liveIndex.push_back(i);
		subsector_t *const ssec = R_PointInSubsector(Particles[i].x, Particles[i].y);
		const int ssnum = int(ssec - subsectors);
		g_particles[k].subsector = ssec;
		g_particles[k].snext = g_firstIn[ssnum];
		g_particles[k].tnext = NO_PARTICLE;
		g_firstIn[ssnum] = k;
	}
	ZX_COUNT("copy/particles", "particles", g_particles.size());
}

}} // namespace zx::gamecopy
