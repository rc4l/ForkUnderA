// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] When a Skulltag score pillar takes a point.

#ifndef ZX_SKULLTAGSCORE_COMPUTE_H
#define ZX_SKULLTAGSCORE_COMPUTE_H

namespace zx {

// [rc4l] Ported from zandronum@f5b68175b468f0fdfb7119369fa10d5ba87f2464: a carrier scores unless the server requires its own skull home and it has been taken.
bool ComputeScoresSkulltagPoint(bool requireSkullToScore, bool ownSkullTaken);

} // namespace zx

#endif
