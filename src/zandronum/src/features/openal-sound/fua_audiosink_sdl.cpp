// [rc4l] SDL sink for the instant-replay audio pump, used on Linux and Windows. See fua_audiosink.h.
//
// This is the original implementation, moved behind the sink interface unchanged. SDL is the
// platform layer on both of those targets anyway, so there is nothing to gain by replacing it --
// only macOS needed a different sink, because the Cocoa backend removed SDL from the engine there.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "fua_audiosink.h"
#include "computation/fua_audiomix_compute.h"

#include "SDL.h"
#include <cstring>

namespace zx
{

namespace
{

FuaAudioFill s_fill = NULL;
bool         s_open = false;

void SDLCallback(void *, Uint8 *stream, int len)
{
	const int frames = FuaFramesForBytes(len);

	if (s_fill != NULL && frames > 0)
	{
		s_fill(reinterpret_cast<short *>(stream), frames);
	}
	else
	{
		std::memset(stream, 0, static_cast<size_t>(len));
	}
}

} // namespace

bool FuaAudioSinkOpen(FuaAudioFill fill, const char **err)
{
	if (s_open)
	{
		if (err != NULL) *err = "already open";
		return false;
	}

	s_fill = fill;

	SDL_InitSubSystem(SDL_INIT_AUDIO);

	SDL_AudioSpec want;
	std::memset(&want, 0, sizeof want);
	want.freq     = kFuaAudioRate;
	want.format   = AUDIO_S16SYS;
	want.channels = kFuaAudioChannels;
	want.samples  = kFuaAudioFrames;
	want.callback = SDLCallback;
	want.userdata = NULL;

	if (SDL_OpenAudio(&want, NULL) != 0)
	{
		if (err != NULL) *err = SDL_GetError();
		s_fill = NULL;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}

	s_open = true;
	SDL_PauseAudio(0);
	return true;
}

void FuaAudioSinkClose()
{
	if (s_open)
	{
		SDL_CloseAudio();
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		s_open = false;
	}
	s_fill = NULL;
}

const char *FuaAudioSinkName()
{
	return "SDL";
}

} // namespace zx
