// [rc4l] Pure decision logic for the OpenAL sound-options menu, extracted from oalsound.cpp so it
// can be unit-tested without linking OpenAL or the engine. Covers the small mappings the menu +
// backend need for UZDoom parity: HRTF tri-state (snd_hrtf), music-mode -> AL stereo/remix modes
// (snd_musicmode), OpenAL resampler name -> index lookup (snd_alresampler), and parsing OpenAL's
// double-NUL-terminated device/name lists. The AL_* values are mirrored here and static_asserted
// against the real al.h/alext.h in the caller. Implementation in oal_menu_compute.cpp.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_OAL_MENU_COMPUTE_H
#define ZX_OAL_MENU_COMPUTE_H

#include <string>
#include <vector>

// [rc4l] HRTF request state, mirroring ALC_FALSE / ALC_TRUE / ALC_DONT_CARE_SOFT (alc.h/alext.h).
enum
{
	ZX_HRTF_DISABLE  = 0, // ALC_FALSE
	ZX_HRTF_ENABLE   = 1, // ALC_TRUE
	ZX_HRTF_DONTCARE = 2, // ALC_DONT_CARE_SOFT (let the driver decide -- "Auto")
};

// [rc4l] snd_musicmode values, mirroring UZDoom's MusicMode enum.
enum
{
	ZX_MUSICMODE_NORMAL      = 0,
	ZX_MUSICMODE_DIRECTMIX   = 1,
	ZX_MUSICMODE_SUPERSTEREO = 2,
};

// [rc4l] AL_STEREO_MODE_SOFT values, mirroring alext.h.
enum
{
	ZX_AL_NORMAL_SOFT       = 0x0000, // AL_NORMAL_SOFT
	ZX_AL_SUPER_STEREO_SOFT = 0x0001, // AL_SUPER_STEREO_SOFT
};

// [rc4l] Map snd_hrtf to the ALC_HRTF_SOFT attribute value: 0 disables, any positive value enables,
// and a negative value ("Auto", the AutoOffOn -1 entry) defers to the driver. Matches UZDoom.
int ComputeHrtfAttrib(int snd_hrtf);

// [rc4l] AL_STEREO_MODE_SOFT for a music mode: super-stereo (UHJ decode) only for SuperStereo,
// otherwise the normal stereo path. Returns a ZX_AL_*_SOFT value.
int ComputeStereoModeSoft(int musicmode);

// [rc4l] Whether AL_DIRECT_CHANNELS_SOFT should remix unmatched channels: true only for DirectMix.
bool ComputeDirectMixRemix(int musicmode);

// [rc4l] Resolve a resampler name to its index within the driver's resampler list. "Default" (or an
// empty string) keeps the driver default without searching -> returns defaultIdx. A name that is
// present returns its index; a non-empty name that is absent returns -1 so the caller can warn and
// fall back to the default. Mirrors UZDoom's snd_alresampler lookup.
int ComputeResamplerIndex(const std::vector<std::string> &names, int defaultIdx, const std::string &wanted);

// [rc4l] Parse OpenAL's double-NUL-terminated string list (as returned by alcGetString for the
// device specifiers) into individual names. A null pointer yields an empty list; the list ends at
// the empty string that follows the final entry.
std::vector<std::string> ParseAlNameList(const char *names);

#endif // ZX_OAL_MENU_COMPUTE_H
