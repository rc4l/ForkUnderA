// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The wait before a Doom-movement player may jump again, which Player.RejumpDelay sets and -1 leaves to the engine's own rules.

#ifndef ZX_REJUMPDELAY_COMPUTE_H
#define ZX_REJUMPDELAY_COMPUTE_H

namespace zx {

// [rc4l] Ported from zandronum@f6f2e6ee1991123e36a712985f37df73bf1e1ace: the jump tics a takeoff gives, the engine's own while RejumpDelay is negative and none when the landing sets the wait.
int ComputeTakeoffJumpTics(int rejumpDelay, bool skulltagJumping, int ticRate);

// [rc4l] Ported from zandronum@f6f2e6ee1991123e36a712985f37df73bf1e1ace: whether a landing delays the next jump, which a RejumpDelay always does.
bool ComputeLandingDelaysJump(int rejumpDelay, bool skulltagJumping, int jumpTics, bool hardLanding);

// [rc4l] The jump tics a landing gives: the engine's 7 while RejumpDelay is negative.
int ComputeLandingJumpTics(int rejumpDelay);

// [rc4l] Adapted from zandronum@f6f2e6ee1991123e36a712985f37df73bf1e1ace: whether a pawn on the floor and still rising, as up a step, delays its next jump, which needs a player to delay, where upstream's dereferenced none.
bool ComputeRisingDelaysJump(bool hasPlayer, int rejumpDelay, bool rising);

// [rc4l] The jump tics while rising on the floor, at least 2 so a step does not double the jump.
int ComputeRisingJumpTics(int rejumpDelay);

} // namespace zx

#endif
