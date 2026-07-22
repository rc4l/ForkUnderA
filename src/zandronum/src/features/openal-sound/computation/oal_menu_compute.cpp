// [rc4l] Implementation of the pure OpenAL sound-menu decision logic. See oal_menu_compute.h.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "oal_menu_compute.h"

int ComputeHrtfAttrib(int snd_hrtf)
{
	if (snd_hrtf == 0)
		return ZX_HRTF_DISABLE;
	if (snd_hrtf > 0)
		return ZX_HRTF_ENABLE;
	return ZX_HRTF_DONTCARE; // negative == "Auto"
}

int ComputeStereoModeSoft(int musicmode)
{
	return musicmode == ZX_MUSICMODE_SUPERSTEREO ? ZX_AL_SUPER_STEREO_SOFT : ZX_AL_NORMAL_SOFT;
}

bool ComputeDirectMixRemix(int musicmode)
{
	return musicmode == ZX_MUSICMODE_DIRECTMIX;
}

int ComputeResamplerIndex(const std::vector<std::string> &names, int defaultIdx, const std::string &wanted)
{
	// [rc4l] The driver default is a name-less request -- don't scan, just keep the default index.
	if (wanted.empty() || wanted == "Default")
		return defaultIdx;

	for (int i = 0; i < static_cast<int>(names.size()); ++i)
	{
		if (names[i] == wanted)
			return i;
	}
	return -1; // named but absent: caller warns and falls back to the default
}

std::vector<std::string> ParseAlNameList(const char *names)
{
	std::vector<std::string> out;
	if (names == nullptr)
		return out;

	// [rc4l] OpenAL specifier lists are a run of NUL-terminated strings ending with an empty one.
	while (*names != '\0')
	{
		std::string name(names);
		names += name.size() + 1;
		out.push_back(std::move(name));
	}
	return out;
}
