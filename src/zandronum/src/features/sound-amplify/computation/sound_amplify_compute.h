// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Pure volume limits for SNDINFO's $fua_allow_amplify, shared by the sound front end and the OpenAL backend.
#ifndef ZX_SOUND_AMPLIFY_COMPUTE_H
#define ZX_SOUND_AMPLIFY_COMPUTE_H

// [Doruk-Aksoy] The loudest a sound under $fua_allow_amplify plays (+6 dB), which is also the most a server sends a client for a call's volume.
constexpr float ZX_MAX_AMPLIFIED_VOLUME = 2.f;

// [Doruk-Aksoy] The volume a sound plays at: the call's volume times its SNDINFO $volume, held to [0, 1], or to [0, ZX_MAX_AMPLIFIED_VOLUME] when $fua_allow_amplify covers the sound.
float ComputeSoundVolume(float volume, float sndinfoVolume, bool allowAmplify);

// [Doruk-Aksoy] A call's volume as a sound command's volume byte, 0..ZX_MAX_AMPLIFIED_VOLUME in steps of 1/127, the encoding servers have always sent.
int ComputeSoundVolumeToWire(float volume);

// [Doruk-Aksoy] A sound command's volume byte as a volume, left above 1 so S_StartSound applies the sound's own limit instead of the client capping it at 1.
float ComputeSoundVolumeFromWire(int volumeByte);

#endif // ZX_SOUND_AMPLIFY_COMPUTE_H
