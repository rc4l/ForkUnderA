// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The game thread: one worker that runs a tic's playsim while the main thread draws, started by the handoff and never waited for by a frame.

#ifndef ZX_HANDOFF_GAMETHREAD_H
#define ZX_HANDOFF_GAMETHREAD_H

namespace zx { namespace handoff {

// [rc4l] Hands work to the game thread, starting it the first time; the handoff's, with no tic in flight.
void StartOnGameThread(void (*work)());
// [rc4l] Whether the work last started has finished, so the handoff may take the game back.
bool GameThreadDone();
// [rc4l] Waits for the work in flight, for a quit or a restart only, never for a frame.
void WaitForGameThread();
// [rc4l] Whether work was started and not yet taken back with TakeBack.
bool TicInFlight();
// [rc4l] Marks the work in flight taken back by the handoff, rethrowing on this thread what the work threw, as an error inline would have.
void TakeBack();

}} // namespace zx::handoff

#endif // ZX_HANDOFF_GAMETHREAD_H
