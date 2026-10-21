// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] "Which of these files do I actually have?", answered without stopping the frame.
//
// WHY THIS EXISTS. Verifying a file means hashing it, and hashing means reading every byte: measured
// at 156ms for one preset naming 157MB of wads. The CUSTOM tab asked that question while DRAWING --
// once per row -- and the NEW tab asked it while restoring the last-played setup. Both were a stall
// on the main thread the moment somebody kept large files outside the by-hash store.
//
// THE THREADING CONTRACT IS features/wad-library's, VERBATIM, and for the same reason. The worker
// touches nothing the engine considers single-threaded: no Printf, no CVARs, no FString -- FString's
// default constructor bumps a refcount on a SHARED NullString and that counter is not atomic, so
// even an empty one is a race. It is handed plain paths before it starts and hands plain paths back
// through a mutex-guarded queue that Tick() drains on the main thread.
//
// THE SPLIT IS waddownload's PlanVerifiedCopy / WalkVerifiedPlan. Planning reads GameConfig and is
// main-thread-only; walking is stat, fopen and an EVP context on the local stack, and is what runs
// here. The synchronous answer and this one come out of the same walk, so they cannot differ.
//
// STALE RESULTS ARE DROPPED, NOT APPLIED. A job carries the caller's `epoch`; the caller bumps its
// own epoch whenever the answer could have changed (a download landing, a preset being saved) and
// ignores anything that comes back stamped with an older one. Nothing here holds a pointer into
// menu state, so a preset deleted mid-flight cannot be written into.

#ifndef ZX_RESOLVEJOB_H
#define ZX_RESOLVEJOB_H

#include <string>
#include <vector>

namespace zx { namespace resolvejob {

// One file to answer for. `key` is the caller's own name for it and is echoed back untouched.
struct Want
{
	std::string key;
	std::string name;
	std::string md5;

	Want() {}
	Want(const std::string &k, const std::string &n, const std::string &m)
		: key(k), name(n), md5(m) {}
};

// What came back. `path` is empty when no copy on this disk has those bytes.
struct Answer
{
	std::string key;
	std::string path;

	Answer() {}
	Answer(const std::string &k, const std::string &p) : key(k), path(p) {}
};

// Start a run. MAIN THREAD ONLY -- it plans each want, which reads GameConfig.
//
// Refuses while one is already running, so calling it from a draw is safe: ask every frame, and the
// second ask through is a bool test. Returns false when it did not start.
//
// `token` is the caller's claim on the result and is echoed back by Tick. There is ONE worker and
// more than one screen wants it -- the CUSTOM list and the NEW tab's restore -- so a token that is
// merely a per-caller counter would eventually collide and one screen would apply the other's
// answers. Callers take theirs from a single shared counter; see the menu's NextVerifyToken.
bool Begin(const std::vector<Want> &wants, int token);

// [rc4l] Claim finished answers, if they are yours. Main thread only, once a frame.
//
// DRAINS ONLY ON A MATCH, which is the whole point of the token: an unclaimed result is left where
// it is rather than swallowed, so the screen that asked for it still gets it when it next looks.
// Anything nobody claims is cleared by the next Begin.
//
// Returns true when `out` was filled.
bool Tick(int token, std::vector<Answer> &out);

// True while a run is in flight, which is what the "Loading..." line is drawn from.
bool Running();

// Abandon the current run. The worker checks between files, so this returns at once; the answers it
// has already queued are dropped by their epoch rather than raced against.
void Cancel();

}} // namespace zx::resolvejob

#endif // ZX_RESOLVEJOB_H
