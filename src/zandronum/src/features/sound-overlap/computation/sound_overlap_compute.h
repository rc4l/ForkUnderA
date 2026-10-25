// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Pure rules for several sounds sharing one channel (uzdoom@e82565373f, uzdoom@4881ec257a), and the server's record of the looping sounds a late joiner is sent.
#ifndef ZX_SOUND_OVERLAP_COMPUTE_H
#define ZX_SOUND_OVERLAP_COMPUTE_H

#include <vector>

// [Doruk-Aksoy] Mirrors s_sound.h's channel and flag values, static_asserted where the engine includes this.
enum
{
	ZX_CHAN_AUTO       = 0,
	ZX_CHAN_LISTENERZ  = 8,
	ZX_CHAN_MAYBE_LOCAL = 16,
	ZX_CHAN_UI         = 32,
	ZX_CHAN_NOPAUSE    = 64,
	ZX_CHAN_LOOP       = 256,
	ZX_CHAN_NOSTOP     = 4096,
	ZX_CHAN_OVERLAP    = 8192,
};

// [Doruk-Aksoy] A sound's channel flags once its channel is known: a CHAN_AUTO sound overlaps, playing on channel 0 beside its source's others instead of taking a free channel.
int ComputeSoundChanFlags(int channel, int chanflags);

// [Doruk-Aksoy] Whether starting a sound stops everything its source already plays on that channel: not for an overlapping sound or one with no source.
bool ComputeStartStopsChannel(int chanflags, bool hasSource);

// [Doruk-Aksoy] uzdoom@e82565373f: whether CHAN_NOSTOP keeps a sound from starting, which it does while the actor already plays that sound on the slot.
bool ComputeNoStopBlocks(int chanflags, bool alreadyPlaying);

// [Doruk-Aksoy] A_StartSound's slot and flags as this engine's channel, slot 0-7 with the flags UZDoom passes on to the sound (CHANF_LOOP is left to the looping path).
int ComputeStartSoundChannel(int slot, int flags);

// [Doruk-Aksoy] One looping sound a server resends to a late joiner: its actor, slot (0-7), the flags it was started with, sound and levels.
struct ZxLoopingSound
{
	void *actor;
	int channel;
	int flags;
	int soundId;
	float volume;
	float attenuation;
};

// [Doruk-Aksoy] The record after a sound starts on actor's channel (slot and flags packed as callers pass them): a non-overlapping start clears what the slot held, and a loop is kept, beside the slot's other loops when it overlaps.
void ComputeLoopingSoundStarted(std::vector<ZxLoopingSound> &list, void *actor, int channel, int soundId,
	float volume, float attenuation, bool looping);

// [Doruk-Aksoy] The record after actor's slot is stopped, which stops every sound on it.
void ComputeLoopingSoundStopped(std::vector<ZxLoopingSound> &list, const void *actor, int slot);

// [Doruk-Aksoy] Whether actor already loops soundId on slot.
bool ComputeIsLooping(const std::vector<ZxLoopingSound> &list, const void *actor, int slot, int soundId);

// [Doruk-Aksoy] The record without actor's loops, or empty when actor is null.
void ComputeForgetLooping(std::vector<ZxLoopingSound> &list, const void *actor);

#endif // ZX_SOUND_OVERLAP_COMPUTE_H
