// [Doruk-Aksoy] See oal_voice_compute.h.
#include "features/openal-sound/computation/oal_voice_compute.h"

bool ComputeVoiceLower(int prio, float dist, int lowestPrio, float lowestDist)
{
	return prio < lowestPrio || (prio == lowestPrio && dist > lowestDist);
}

void ComputeMemoBegin(LowestVoiceMemo &m)
{
	m.depth++;
}

void ComputeMemoEnd(LowestVoiceMemo &m)
{
	if (m.depth > 0) m.depth--;
	if (m.depth == 0) m.valid = false;
}

bool ComputeMemoRecall(const LowestVoiceMemo &m, const void *&lowest)
{
	if (m.depth <= 0 || !m.valid) return false;
	lowest = m.lowest;
	return true;
}

void ComputeMemoKeep(LowestVoiceMemo &m, const void *lowest)
{
	if (m.depth <= 0) return;
	m.lowest = lowest;
	m.valid = true;
}

void ComputeMemoForget(LowestVoiceMemo &m)
{
	m.valid = false;
}
