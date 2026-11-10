// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_LEVELAUTH_COMPUTE_H
#define ZX_LEVELAUTH_COMPUTE_H

namespace zx { namespace netsession {

// [Doruk-Aksoy] What a joining client's answer to SVCC_AUTHENTICATE earns.
enum class LevelAuthVerdict
{
	RejectIdentity,
	AskAgain,
	RejectLevel,
	Accept,
};

// [Doruk-Aksoy] The verdict on a joiner's answer, asked again rather than refused when the server changed map after naming askedMap (empty when unknown).
LevelAuthVerdict ComputeLevelAuthVerdict(bool identityHolds, const char *askedMap, const char *currentMap, bool checksumMatches);

}} // namespace zx::netsession

#endif
