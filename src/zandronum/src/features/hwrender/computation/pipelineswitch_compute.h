// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] The pipeline switches a draw loop makes over its draws in order, tallied by the pair of pipelines each switch goes between.

#ifndef ZX_PIPELINESWITCH_COMPUTE_H
#define ZX_PIPELINESWITCH_COMPUTE_H

#include <vector>

namespace zx { namespace hwrender {

// [Doruk-Aksoy] A draw's pipeline and the GLDEFS shader it runs (-1 for none), which together pick the program it binds.
struct PipelineKey
{
	int pipeline;
	int user;
};

// [Doruk-Aksoy] How often a loop switched from pipeline `from` to pipeline `to`, a shader change within one pipeline counting as a switch to itself.
struct PipelineSwitchCount
{
	int from, to, count;
};

// [Doruk-Aksoy] The switches between consecutive draws whose key differs, the loop's first bind not among them, by pair, most frequent first and then by pair.
std::vector<PipelineSwitchCount> ComputePipelineSwitches(const std::vector<PipelineKey> &draws);

// [Doruk-Aksoy] The switches a tally holds in all.
int ComputeSwitchTotal(const std::vector<PipelineSwitchCount> &tally);

}} // namespace zx::hwrender

#endif // ZX_PIPELINESWITCH_COMPUTE_H
