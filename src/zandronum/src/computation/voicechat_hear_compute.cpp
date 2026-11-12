// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/voicechat_hear_compute.h"

namespace zx { namespace voice {

bool ComputeVoiceForbiddenToHear(bool validSpeaker, bool speakerIsLocal, bool speakerIgnored, bool listenerSpectating, bool speakerForbiddenToChat)
{
	if (!validSpeaker)
		return true;

	// [rc4l] The local player always hears their own microphone test.
	if (speakerIsLocal)
		return false;

	if (speakerIgnored)
		return true;

	return !listenerSpectating && speakerForbiddenToChat;
}

}} // namespace zx::voice
