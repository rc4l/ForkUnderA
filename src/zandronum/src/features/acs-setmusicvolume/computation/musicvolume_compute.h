// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Pure checks for the music volume factor ACS SetMusicVolume sets (uzdoom@a1e8d83724).
#ifndef ZX_MUSICVOLUME_COMPUTE_H
#define ZX_MUSICVOLUME_COMPUTE_H

// [Doruk-Aksoy] The factor a client applies from a server's FuaSetMusicVolume: held to I_SetMusicVolume's 0-2, and 1 for a value that is not a number, which that clamp would let through.
float ComputeNetMusicVolume(float volume);

#endif // ZX_MUSICVOLUME_COMPUTE_H
