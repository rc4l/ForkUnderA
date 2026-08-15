// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_resolvejob.h for the design and the threading contract it inherits.

#include "features/wad-download/zx_resolvejob.h"

#include <atomic>
#include <mutex>
#include <thread>

#include "features/wad-download/computation/fileresolve_compute.h"
#include "features/wad-download/computation/jobstate_compute.h"
#include "features/wad-download/zx_waddownload.h"

namespace zx { namespace resolvejob {

namespace
{

// ---------------------------------------------------------------- shared with the worker
//
// std::string and PODs only. Everything the worker needs is copied in before it starts, and
// everything it produces goes through this queue -- it never reads a caller's memory.
std::mutex g_mutex;
std::vector<Answer> g_done;
int g_doneEpoch = -1;		// the token the finished run was started with
bool g_haveDone = false;

std::atomic<bool> g_cancel(false);
std::atomic<bool> g_running(false);

// One file's worth of work, already planned. The plan is built on the main thread because planning
// reads GameConfig; walking it is the part that reads bytes, and the part that runs here.
struct Task
{
	std::string key;
	std::string md5;
	std::vector<zx::ResolveStep> steps;
};

// [rc4l] BY VALUE, deliberately. The thread owns its own copy of the whole work list, so nothing it
// touches can be freed underneath it -- there is no pointer back into the caller at all. The cost is
// one copy of a few hundred short strings, which is nothing beside the hashing it is about to do.
void RunResolve(std::vector<Task> tasks, int token)
{
	std::vector<Answer> answers;
	answers.reserve(tasks.size());

	for (size_t i = 0; i < tasks.size(); ++i)
	{
		// Checked between files rather than within one: a hash already under way is cheaper to
		// finish than to restart, and the caller drops the whole result by epoch anyway.
		if (g_cancel.load())
			break;

		answers.push_back(Answer(tasks[i].key,
			zx::waddownload::WalkVerifiedPlan(tasks[i].steps, tasks[i].md5)));
	}

	std::lock_guard<std::mutex> lock(g_mutex);
	g_done.swap(answers);
	g_doneEpoch = token;
	g_haveDone = true;
	g_running.store(false);
}

} // namespace

bool Begin(const std::vector<Want> &wants, int token)
{
	// The rule, not a copy of it: computation/jobstate_compute, which the tests hold to.
	if (!zx::JobAcceptsBegin(g_running.load(), wants.size()))
		return false;

	// [rc4l] Planned HERE, on the main thread, because PlanVerifiedCopy reads GameConfig and builds
	// FStrings. This is the stat-only half; what goes to the worker is plain paths.
	std::vector<Task> tasks;
	tasks.reserve(wants.size());

	for (size_t i = 0; i < wants.size(); ++i)
	{
		Task task;
		task.key = wants[i].key;
		task.md5 = wants[i].md5;
		task.steps = zx::waddownload::PlanVerifiedCopy(wants[i].name.c_str(),
			wants[i].md5.empty() ? NULL : wants[i].md5.c_str());

		tasks.push_back(task);
	}

	{
		// Anything a cancelled run left behind goes now, so it cannot be drained as if it belonged
		// to this one. The epoch would catch it; clearing means it never has to.
		std::lock_guard<std::mutex> lock(g_mutex);
		g_done.clear();
		g_doneEpoch = -1;
		g_haveDone = false;
	}

	g_cancel.store(false);
	g_running.store(true);

	// Detached, the same shape as the library scan and the downloader. The thread owns `tasks` and
	// unwinds on its own; there is nothing to join and nothing to free.
	std::thread(RunResolve, tasks, token).detach();
	return true;
}

bool Tick(int token, std::vector<Answer> &out)
{
	std::lock_guard<std::mutex> lock(g_mutex);

	if (!g_haveDone)
		return false;

	// Somebody else's. Left alone rather than dropped: they are still waiting for it, and this is
	// the one place it exists.
	if (!zx::JobAcceptsResult(g_doneEpoch, token))
		return false;

	out.swap(g_done);
	g_done.clear();
	g_haveDone = false;

	return true;
}

bool Running()
{
	return g_running.load();
}

void Cancel()
{
	g_cancel.store(true);
}

}} // namespace zx::resolvejob
