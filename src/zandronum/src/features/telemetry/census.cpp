// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The engine core's counters, which no feature owns: what the level holds (actors, thinkers, its map), the engine's own heap, the texture and sound tables, and the channels playing.

#include "features/telemetry/telemetry.h"

#include "actor.h"
#include "dobject.h"
#include "doomstat.h"
#include "dthinker.h"
#include "r_state.h"
#include "s_sound.h"
#include "textures/textures.h"

namespace {

using zx::telemetry::CounterKind;
using zx::telemetry::CounterSpec;
using zx::telemetry::Register;

bool InLevel()
{
	return gamestate == GS_LEVEL;
}

// [rc4l] How many thinkers of class T the level holds.
template <class T> double CountThinkers()
{
	if (!InLevel()) return 0.0;
	int n = 0;
	TThinkerIterator<T> it;
	while (it.Next() != NULL) n++;
	return n;
}

double Sectors() { return InLevel() ? numsectors : 0.0; }
double Lines() { return InLevel() ? numlines : 0.0; }
double Subsectors() { return InLevel() ? numsubsectors : 0.0; }
double EngineHeap() { return (double)GC::AllocBytes; }
double Textures() { return TexMan.NumTextures(); }

double SoundChannels()
{
	int n = 0;
	for (FSoundChan *c = Channels; c != NULL; c = c->NextChan) n++;
	return n;
}

double SoundsLoaded()
{
	int n = 0;
	for (unsigned i = 0; i < S_sfx.Size(); i++) if (S_sfx[i].data.isValid()) n++;
	return n;
}

// [rc4l] What the level and the tables hold is read once a second, the heap and the channels twice.
const bool s_registered = (
	Register(CounterSpec{ "census/actors", "objects", CounterKind::Gauge, 1000 }, CountThinkers<AActor>),
	Register(CounterSpec{ "census/thinkers", "objects", CounterKind::Gauge, 1000 }, CountThinkers<DThinker>),
	Register(CounterSpec{ "census/sectors", "objects", CounterKind::Gauge, 1000 }, Sectors),
	Register(CounterSpec{ "census/lines", "objects", CounterKind::Gauge, 1000 }, Lines),
	Register(CounterSpec{ "census/subsectors", "objects", CounterKind::Gauge, 1000 }, Subsectors),
	Register(CounterSpec{ "mem/engine_heap_bytes", "bytes", CounterKind::Gauge, 500 }, EngineHeap),
	Register(CounterSpec{ "census/textures", "objects", CounterKind::Gauge, 1000 }, Textures),
	Register(CounterSpec{ "census/sounds_loaded", "objects", CounterKind::Gauge, 1000 }, SoundsLoaded),
	Register(CounterSpec{ "audio/channels", "channels", CounterKind::Gauge, 500 }, SoundChannels),
	true);

} // namespace
