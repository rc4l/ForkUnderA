// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Implementation of the channel-overlap rules; no engine dependencies.
#include "features/sound-overlap/computation/sound_overlap_compute.h"
#include <algorithm>

namespace
{
// [Doruk-Aksoy] Removes every entry pred accepts.
template <typename Pred>
void EraseIf(std::vector<ZxLoopingSound> &list, Pred pred)
{
	list.erase(std::remove_if(list.begin(), list.end(), pred), list.end());
}
}

int ComputeSoundChanFlags(int channel, int chanflags)
{
	// [Doruk-Aksoy] uzdoom@4881ec257a: CHAN_AUTO used to hijack a free channel, and with all eight in use the sound was dropped.
	return channel == ZX_CHAN_AUTO ? (chanflags | ZX_CHAN_OVERLAP) : chanflags;
}

bool ComputeStartStopsChannel(int chanflags, bool hasSource)
{
	return hasSource && !(chanflags & ZX_CHAN_OVERLAP);
}

bool ComputeNoStopBlocks(int chanflags, bool alreadyPlaying)
{
	return (chanflags & ZX_CHAN_NOSTOP) && alreadyPlaying;
}

int ComputeStartSoundChannel(int slot, int flags)
{
	const int passed = ZX_CHAN_LISTENERZ | ZX_CHAN_MAYBE_LOCAL | ZX_CHAN_UI | ZX_CHAN_NOPAUSE | ZX_CHAN_NOSTOP | ZX_CHAN_OVERLAP;
	return (slot & 7) | (flags & passed);
}

void ComputeLoopingSoundStarted(std::vector<ZxLoopingSound> &list, void *actor, int channel, int soundId,
	float volume, float attenuation, bool looping)
{
	const int slot = channel & 7;
	const int flags = channel & ~7;
	const bool overlaps = (ComputeSoundChanFlags(slot, flags) & ZX_CHAN_OVERLAP) != 0;

	// [Doruk-Aksoy] A sound CHAN_NOSTOP keeps from starting, as clients do while the same loop plays, changes nothing.
	if (ComputeNoStopBlocks(flags, ComputeIsLooping(list, actor, slot, soundId)))
		return;

	// [Doruk-Aksoy] Clients stop the slot for a sound that does not overlap, and the same loop started again replaces itself.
	EraseIf(list, [&](const ZxLoopingSound &s)
	{
		return s.actor == actor && s.channel == slot && (!overlaps || (looping && s.soundId == soundId));
	});
	if (looping)
		list.push_back(ZxLoopingSound{ actor, slot, flags, soundId, volume, attenuation });
}

void ComputeLoopingSoundStopped(std::vector<ZxLoopingSound> &list, const void *actor, int slot)
{
	EraseIf(list, [&](const ZxLoopingSound &s) { return s.actor == actor && s.channel == slot; });
}

bool ComputeIsLooping(const std::vector<ZxLoopingSound> &list, const void *actor, int slot, int soundId)
{
	return std::any_of(list.begin(), list.end(), [&](const ZxLoopingSound &s)
	{
		return s.actor == actor && s.channel == slot && s.soundId == soundId;
	});
}

void ComputeForgetLooping(std::vector<ZxLoopingSound> &list, const void *actor)
{
	if (actor == nullptr)
		list.clear();
	else
		EraseIf(list, [&](const ZxLoopingSound &s) { return s.actor == actor; });
}
