// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The rules a background job follows, with the thread taken out.
//
// There are three detached workers in this tree now -- the downloader, the WAD library scan, and the
// file resolver -- and each grew its own answer to the same three questions: may a run start, is an
// arriving result still wanted, and when has the question changed underneath it. Those answers are
// where the bugs live, and they are the one part of a worker that never needed a thread to be true.
//
// A result arriving late is the whole difficulty. The worker is reading files while the world moves:
// a download lands, a preset is saved, the player leaves the screen. By the time it speaks, it may be
// answering a question nobody asked any more, and writing that answer into the cache is how a menu
// ends up confidently showing something that was true a second ago. Stamping the run and comparing
// on arrival is the fix, and it is four lines that are easy to write slightly wrong -- so they live
// here, with tests, rather than three times over.
//
// EPOCHS FOLD SEVERAL COUNTERS INTO ONE. A caller usually has more than one reason to invalidate
// (files appeared; presets changed), and none of those counters is monotonic on its own -- they are
// bumped by unrelated code and can even go backwards across a reload. What a job can be stamped with
// has to only ever go up, so NextEpoch takes the current inputs, notices any change, and advances a
// number that does.
//
// Header-pure by the features/ rules -- no engine types, no threading primitives. What uses it holds
// the mutex; this only says what the answer is.

#ifndef ZX_JOBSTATE_COMPUTE_H
#define ZX_JOBSTATE_COMPUTE_H

// [rc4l] <cstddef> for std::size_t, which this header names in its own interface. <vector> happens
// to drag it in on MSVC and does not on libstdc++, so leaving it out built here and failed on the
// Linux server image with "'i' was not declared in this scope" -- the size_t in the loop header had
// silently not been a type at all.
#include <cstddef>
#include <vector>

namespace zx
{

// [rc4l] WHAT A BUSY WORKER MEANS is the one thing the three of them do NOT agree on, and it is the
// difference worth naming rather than flattening.
//
// The file resolver and the library scan REFUSE: their work is a question about the disk, the answer
// will be the same in a moment, and a second run would read the same bytes twice. The downloader
// DEFERS: its work is a request somebody just made -- they picked another server -- so dropping it
// would lose what was asked for, and it queues the new one behind cancelling the old.
//
// Passing that policy in is what lets one function answer for all three without any of them
// pretending to be another.
enum class JobWhenBusy
{
	Refuse,
	Defer,
};

enum class JobStart
{
	Start,
	Refuse,
	Defer,
};

// `bRunning` is the live flag the worker clears when it finishes. `workCount` is how much there is
// to do -- zero is not "start an empty run", it is "there is nothing to ask", and starting a thread
// to do nothing is a thread that still has to be created and scheduled by the OS.
//
// Nothing to do beats every other consideration, INCLUDING deferring: queueing an empty job would
// have the downloader cancel a run in flight to make room for nothing.
JobStart JobDecideStart(bool bRunning, std::size_t workCount, JobWhenBusy whenBusy);

// Whether a new run may start now, for a caller that refuses while busy. JobDecideStart with the
// Refuse policy, named for the common case.
bool JobAcceptsBegin(bool bRunning, std::size_t workCount);

// [rc4l] Whether a scan that runs ONCE PER SESSION should run now.
//
// The library's rule, which is not the resolver's: it is not answering a question somebody asked, it
// is filling a list that only has to be built once, so having already started is itself a reason not
// to start again. `bForce` is the explicit rebuild that overrides that, and it still will not stack
// a second run on top of one already going.
bool JobAcceptsRescan(bool bRunning, bool bStarted, bool bForce);

// Whether a result stamped `resultEpoch` still answers the question being asked at `currentEpoch`.
//
// Equality rather than >=: an epoch only ever goes up, so a result from the future cannot exist, and
// treating one as acceptable would hide a caller that stamped its job wrong.
bool JobAcceptsResult(int resultEpoch, int currentEpoch);

// [rc4l] The epoch to use now, given every counter the caller invalidates on.
//
// `previous` is what those counters were last time; `now` is what they are. When any differs the
// epoch advances by one and the caller must throw away what it had cached. Returns the epoch to use
// and, through `bChanged`, whether this call is the one that noticed.
//
// The vectors are compared elementwise, so a caller can add a third reason to invalidate without
// touching this. A size mismatch counts as a change: it means the caller itself was rebuilt.
int JobNextEpoch(int epoch, const std::vector<int> &previous, const std::vector<int> &now,
                 bool &bChanged);

} // namespace zx

#endif // ZX_JOBSTATE_COMPUTE_H
