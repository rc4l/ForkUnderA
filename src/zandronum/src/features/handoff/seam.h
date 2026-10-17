// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Every place work crosses between the game and the main thread, declared once with ZX_SEAM: that one line checks the thread, times it, names it in a crash report, beats its thread's heartbeat and delays it under the stress mode.

#ifndef ZX_HANDOFF_SEAM_H
#define ZX_HANDOFF_SEAM_H

#include "features/handoff/computation/handoff_compute.h"
#include "features/telemetry/telemetry.h"

#include <cstdint>

namespace zx { namespace handoff {

// [rc4l] Marks the calling thread as the main thread, once at startup.
void NoteMainThread();
// [rc4l] Marks the calling thread as the one running tics; the main thread is it while tics run inline.
void NoteGameThread();
bool OnMainThread();
bool OnGameThread();

// [rc4l] Whether a tic is running and whether the main thread is in the handoff; until the loop sets them the main thread counts as always in the handoff.
void SetGameRunning(bool running);
void SetMainInHandoff(bool inHandoff);
Phase CurrentPhase();

// [rc4l] A seam's fixed part, made the first time its ZX_SEAM runs.
struct SeamSite
{
	SeamSite(const char *name, Owner owner, bool timed, const char *function, int line);
	int index;
	const char *name;
	const char *function;   // [rc4l] the function and line it sits in, since many sites share one name
	int line;
	Owner owner;
	zx::telemetry::Counter counter;   // [rc4l] its timing, -1 when untimed
};

// [rc4l] Enters a seam for its scope.
class SeamScope
{
public:
	explicit SeamScope(const SeamSite &site);
	~SeamScope();
	SeamScope(const SeamScope &) = delete;
	SeamScope &operator=(const SeamScope &) = delete;

private:
	const char *m_outer;
	int64_t m_start;
	const SeamSite *m_site;
};

// [rc4l] The seam the main thread (game false) or the game thread (game true) is in, or NULL, for a crash report or the hang watchdog.
const char *CurrentSeam(bool game);
// [rc4l] Passes through a thread's seams, which the hang watchdog watches for each thread.
uint64_t Heartbeat(bool game);
// [rc4l] Entries a disallowed thread made, over every seam.
uint64_t SeamViolations();

#define ZX_SEAM_CAT2(a, b) a##b
#define ZX_SEAM_CAT(a, b) ZX_SEAM_CAT2(a, b)
// [rc4l] Declares and enters the seam name (a "group/name" literal, also its timing counter) owned by owner for the rest of the scope.
#define ZX_SEAM(name, owner) \
	static const zx::handoff::SeamSite ZX_SEAM_CAT(zxSeamSite, __LINE__)(name, zx::handoff::Owner::owner, true, __func__, __LINE__); \
	const zx::handoff::SeamScope ZX_SEAM_CAT(zxSeam, __LINE__)(ZX_SEAM_CAT(zxSeamSite, __LINE__))
// [rc4l] ZX_SEAM for a crossing too frequent to time (a lock, a sound), which is still checked, named and stressed.
#define ZX_SEAM_UNTIMED(name, owner) \
	static const zx::handoff::SeamSite ZX_SEAM_CAT(zxSeamSite, __LINE__)(name, zx::handoff::Owner::owner, false, __func__, __LINE__); \
	const zx::handoff::SeamScope ZX_SEAM_CAT(zxSeam, __LINE__)(ZX_SEAM_CAT(zxSeamSite, __LINE__))

}} // namespace zx::handoff

#endif // ZX_HANDOFF_SEAM_H
