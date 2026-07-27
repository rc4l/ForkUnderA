// [MGOOOOOO] Pure decision logic for the A_JumpIfInput DECORATE action, extracted so the
// button-matching and the "send full buttons" protocol predicate can be unit-tested without
// linking the engine. The engine glue (thingdef_codeptr.cpp, cl_commands.cpp) is a thin wrapper
// that feeds player->cmd.ucmd.buttons / player->oldbuttons into these. Implementation in
// jumpifinput_compute.cpp.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#ifndef ZX_JUMPIFINPUT_COMPUTE_H
#define ZX_JUMPIFINPUT_COMPUTE_H

// [MGOOOOOO] Match-behavior flags for A_JumpIfInput's `flags` parameter. Kept in sync with the
// JIF_* constants exposed to DECORATE in wadsrc/static/actors/constants.txt.
enum
{
	JIF_ALL  = 1, // [MGOOOOOO] Require every listed button (default: any single one matches).
	JIF_EDGE = 2, // [MGOOOOOO] Only match on the tic the input becomes newly satisfied.
	JIF_NOT  = 4, // [MGOOOOOO] Invert the final result.
};

// [MGOOOOOO] True when the given button test should trigger a jump. `keys` is an OR-mask of the
// BT_* button bits; `buttons`/`oldbuttons` are this and the previous tic's pressed buttons. A
// zero `keys` mask never matches (a no-op guard, before inversion). See constants.txt for BT_*.
bool ComputeInputMatch(unsigned int buttons, unsigned int oldbuttons, int keys, int flags);

// [MGOOOOOO] True when the client's move command must transmit the full 32-bit button set instead
// of just the low gameplay byte: whenever a script/user button above bit 7 is pressed, or when
// the compat flag forces it. Needed so server-authoritative A_JumpIfInput can see user buttons.
bool ComputeShouldSendFullButtons(unsigned int buttons, bool forceFull);

#endif // ZX_JUMPIFINPUT_COMPUTE_H
