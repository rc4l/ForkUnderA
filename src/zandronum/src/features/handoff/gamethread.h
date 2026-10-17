// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The game thread: one worker that runs a tic's playsim while the main thread draws, started by the handoff and never waited for by a frame.

#ifndef ZX_HANDOFF_GAMETHREAD_H
#define ZX_HANDOFF_GAMETHREAD_H

#include <cstdint>
#include <type_traits>

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
// [rc4l] Runs fn(arg) on the main thread, at once inline or, from a tic on its own thread, by asking and waiting up to a frame, for work only the main thread may do whose answer the tic needs now.
void AskMain(void (*fn)(void *), void *arg);
// [rc4l] AskMain with a lambda, which captures what the main thread reads and writes for the tic.
template <typename F> void AskMainFor(F &&call)
{
	AskMain([](void *p) { (*static_cast<typename std::remove_reference<F>::type *>(p))(); }, &call);
}
// [rc4l] Runs what the game thread asked for and waits on, which the main thread does every frame beside a tic.
void ServeAsks();
// [rc4l] The calls the game thread asked of the main thread, since the start.
uint64_t AsksServed();
// [rc4l] Whether the game thread is waiting on an ask, which the hang watchdog leaves to the main thread's own check.
bool GameThreadAsking();

}} // namespace zx::handoff

#endif // ZX_HANDOFF_GAMETHREAD_H
