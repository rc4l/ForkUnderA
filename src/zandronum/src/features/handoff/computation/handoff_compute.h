// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The rules the game and the main thread share data by: who may enter a seam, the stress mode's delays, and the registry every seam is declared in once.

#ifndef ZX_HANDOFF_COMPUTE_H
#define ZX_HANDOFF_COMPUTE_H

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

namespace zx { namespace handoff {

// [rc4l] Who a seam belongs to: the main thread, the thread running tics, game data (the running tic, or the main thread in the handoff), the handoff itself, the main thread while no tic runs, in a frame or not, or anyone.
enum class Owner { Main, GameThread, GameData, Handoff, Idle, Any };

// [rc4l] Where the two threads are: whether a tic is running, and whether the main thread is in the handoff, the stretch between tics when it may touch game data.
struct Phase { bool gameRunning; bool mainInHandoff; };

// [rc4l] Whether a thread may enter a seam of owner; with the tic run inline the main thread is also the game thread, and draws outside the handoff as it would beside a running game.
bool ComputeOwnerAllows(Owner owner, bool onMain, bool onGame, Phase phase);

// [rc4l] Whether the calling thread is the main one drawing beside a tic on the game thread, so what it does to game data, as a focus change pausing the game does, waits for the handoff.
bool ComputeBesideTic(bool onMain, bool onGame, Phase phase);

// [rc4l] The owner's name as a seam listing prints it.
const char *ComputeOwnerName(Owner owner);

// [rc4l] A stress delay in microseconds for one entry into a seam: about one entry in eight waits up to maxUs, the same for the same seed, seam and count.
uint32_t ComputeStressDelayUs(uint64_t seed, uint32_t seam, uint64_t count, uint32_t maxUs);

// [rc4l] A declared seam: its name, owner, how often it was entered and how often by a thread its owner does not allow.
struct SeamInfo
{
	SeamInfo(const char *n, Owner o) : name(n), owner(o), entries(0), violations(0) {}
	std::string name;
	Owner owner;
	std::atomic<uint64_t> entries;
	std::atomic<uint64_t> violations;
};

// [rc4l] Every seam, declared once by name from any thread; a name declared again returns the first index, and a second owner for it is a conflict.
class SeamRegistry
{
public:
	// [rc4l] The seam's index, or -1 when name was declared before with another owner.
	int Declare(const char *name, Owner owner);
	// [rc4l] Counts an entry, and a violation when allowed is false.
	void Enter(int seam, bool allowed);
	int Size() const;
	const SeamInfo &At(int seam) const;
	// [rc4l] Total entries a disallowed thread made, over every seam.
	uint64_t Violations() const;

private:
	mutable std::mutex m_mutex;
	std::deque<SeamInfo> m_seams;
};

// [rc4l] The seams as a listing prints them, one per line: name, owner, entries and violations.
std::string ComputeSeamTable(const SeamRegistry &registry);

// [rc4l] Where disallowed threads entered seams, a row per site, thread and phase, so a report names the code and the thread.
class ViolationSites
{
public:
	// [rc4l] Counts a disallowed entry into seam at function:line (the site's identity being `site`), by thread 0 main, 1 game or 2 any other, in phase.
	void Count(const void *site, const char *seam, const char *function, int line, int thread, Phase phase);
	// [rc4l] The rows, the most entered first: seam, function:line, which thread, whether a tic ran and the main thread was in the handoff, and how often.
	std::string Table() const;

private:
	struct Row
	{
		const void *site;
		std::string seam, where;
		int thread;
		Phase phase;
		uint64_t count;
	};
	mutable std::mutex m_mutex;
	std::deque<Row> m_rows;
};

}} // namespace zx::handoff

#endif // ZX_HANDOFF_COMPUTE_H
