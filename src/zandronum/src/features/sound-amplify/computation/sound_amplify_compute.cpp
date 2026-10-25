// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Implementation of the sound volume limits; no engine dependencies.
#include "features/sound-amplify/computation/sound_amplify_compute.h"
#include <algorithm>

float ComputeSoundVolume(float volume, float sndinfoVolume, bool allowAmplify)
{
	const float limit = allowAmplify ? ZX_MAX_AMPLIFIED_VOLUME : 1.f;
	return std::min(std::max(volume * sndinfoVolume, 0.f), limit);
}

int ComputeSoundVolumeToWire(float volume)
{
	return int(std::min(std::max(volume, 0.f), ZX_MAX_AMPLIFIED_VOLUME) * 127);
}

float ComputeSoundVolumeFromWire(int volumeByte)
{
	return float(volumeByte) / 127.f;
}
