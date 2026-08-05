// [rc4l] The output sink for the instant-replay audio pump.
//
// Why this exists: instant replay opens OpenAL's ALC_SOFT_loopback device so the recorder can
// capture the master mix. A loopback device renders on demand and drives no clock of its own, so
// something must pull at the output rate and play the result to a real device. That "something" is
// the only platform-specific part, and it is what this hides.
//
// macOS uses AudioQueue, because the Cocoa backend removed SDL from the engine entirely and pulling
// libSDL2 back in for one callback would undo that. Linux and Windows keep the SDL implementation:
// SDL is their platform layer anyway, so there is nothing to gain by replacing it.
//
// Instant replay has no upstream counterpart -- GZDoom's oalsound uses a normal, self-driving
// device and contains no SDL at all -- so there is no upstream shape to be faithful to here, and
// per the fua-naming rule these are ours and carry fua_.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_FUA_AUDIOSINK_H
#define ZX_FUA_AUDIOSINK_H

namespace zx
{

// [rc4l] Called from the audio thread to fill `frames` interleaved stereo S16 samples. It must not
// block, allocate or take engine locks -- it runs on the OS's realtime audio callback.
typedef void (*FuaAudioFill)(short *out, int frames);

// [rc4l] Start pulling. Returns false and leaves nothing open on failure; the caller then plays
// silent, which is a degraded replay rather than a dead engine. `err` receives a short reason.
bool FuaAudioSinkOpen(FuaAudioFill fill, const char **err);

// [rc4l] Stop and release. Safe to call when nothing was opened.
void FuaAudioSinkClose();

// [rc4l] Name of the backing API, for the startup log.
const char *FuaAudioSinkName();

} // namespace zx

#endif // ZX_FUA_AUDIOSINK_H
