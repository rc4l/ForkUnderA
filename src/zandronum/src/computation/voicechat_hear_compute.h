// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Whose voice the local player may hear, the rule behind VOIPController::IsForbiddenToHearPlayer.

#ifndef ZX_VOICECHAT_HEAR_COMPUTE_H
#define ZX_VOICECHAT_HEAR_COMPUTE_H

namespace zx { namespace voice {

// [rc4l] Ported from zandronum@52bc0caca76d1632afe369dbcc9c91373a71a5ac: no invalid or ignored speaker, and a live listener hears no spectator forbidden to chat with players.
bool ComputeVoiceForbiddenToHear(bool validSpeaker, bool speakerIsLocal, bool speakerIgnored, bool listenerSpectating, bool speakerForbiddenToChat);

}} // namespace zx::voice

#endif // ZX_VOICECHAT_HEAR_COMPUTE_H
