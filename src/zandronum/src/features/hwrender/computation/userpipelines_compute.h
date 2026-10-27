// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] GLDEFS shaders' pipelines of one kind (the world's or the 2D layer's) by (pipeline, shader), each made on the device's pool and finished, its statics bound and its bindings checked, once it is ready, so no frame waits on the driver for one.

#ifndef ZX_HWRENDER_USERPIPELINES_COMPUTE_H
#define ZX_HWRENDER_USERPIPELINES_COMPUTE_H

#include <map>
#include <memory>
#include <utility>

namespace zx { namespace hwrender {

// [Doruk-Aksoy] What a pipeline's status reads: still compiling, ready, or failed.
enum { kUserPipelineCompiling, kUserPipelineReady, kUserPipelineFailed };

// [Doruk-Aksoy] The pipelines started, each pending until it is finished; an empty one is left out, for its plain one to draw.
template <class Pso> struct UserPipelines
{
	struct Entry { Pso pso; bool pending = false; };
	std::map<std::pair<int, int>, Entry> made;
};

// [Doruk-Aksoy] Entry `e` of `key` once `status` (waiting for it when `wait`) reads its pipeline done: handed to `finish`, empty if it failed, and kept only if that returns true; false while it still compiles.
template <class Pso, class Status, class Finish>
bool ComputeLandUserPipeline(const std::pair<int, int> &key, typename UserPipelines<Pso>::Entry &e, bool wait, Status status, Finish finish)
{
	if (!e.pending) return true;
	const int s = e.pso ? status(e.pso, wait) : kUserPipelineFailed;
	if (s == kUserPipelineCompiling) return false;
	e.pending = false;
	if (s == kUserPipelineFailed) e.pso = Pso();
	if (!finish(key, e.pso)) e.pso = Pso();
	return true;
}

// [Doruk-Aksoy] The pipeline for `key`, started by `start` the first time it is asked for and landed as above; null while it compiles and once left out, for the caller to draw with its plain one.
template <class Pso, class Start, class Status, class Finish>
Pso *ComputeUserPipeline(UserPipelines<Pso> &table, const std::pair<int, int> &key, bool wait, Start start, Status status, Finish finish)
{
	auto found = table.made.find(key);
	if (found == table.made.end())
	{
		typename UserPipelines<Pso>::Entry started;
		started.pso = start();
		started.pending = true;
		found = table.made.emplace(key, started).first;
	}
	typename UserPipelines<Pso>::Entry &e = found->second;
	if (!ComputeLandUserPipeline<Pso>(key, e, wait, status, finish)) return nullptr;
	return e.pso ? std::addressof(e.pso) : nullptr;
}

// [Doruk-Aksoy] Every pipeline still compiling, waited for and landed, so what a level asked for is ready before its first frame.
template <class Pso, class Status, class Finish>
void ComputeLandAllUserPipelines(UserPipelines<Pso> &table, Status status, Finish finish)
{
	for (auto &m : table.made) ComputeLandUserPipeline<Pso>(m.first, m.second, true, status, finish);
}

}} // namespace zx::hwrender

#endif // ZX_HWRENDER_USERPIPELINES_COMPUTE_H
