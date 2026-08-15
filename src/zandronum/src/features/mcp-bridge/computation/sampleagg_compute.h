// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The pure half of the sampling profiler: folding raw hits into a ranked table.
//
// A sampler interrupts the game thread N times a second and writes down where it was. That part is
// per-platform and full of handles. THIS part -- counting, ranking, turning counts into percentages
// and writing the report -- is the same everywhere and is where the arithmetic can actually be
// wrong, so it lives here where it can be tested without a running engine.

#ifndef ZX_SAMPLEAGG_COMPUTE_H
#define ZX_SAMPLEAGG_COMPUTE_H

#include <cstddef>
#include <string>
#include <vector>

namespace zx
{

// Where one sample landed: the function the instruction pointer was inside, and the module that
// function came from. Both may be empty when the address could not be resolved -- an unresolved
// sample still counts, because dropping it would quietly inflate everything else's share.
struct SampleHit
{
	std::string symbol;
	std::string dso;

	SampleHit();
	SampleHit(const std::string &symbol, const std::string &dso);
};

// One row of the answer: a function, how many samples landed in it, and what share of the run that
// is.
struct SampleFunc
{
	std::string symbol;
	std::string dso;
	unsigned long long count;
	double percent;

	SampleFunc();
	SampleFunc(const std::string &symbol, const std::string &dso, unsigned long long count,
		double percent);
};

// Fold hits into ranked functions, hottest first, keeping at most `top` (0 keeps all). Percentages
// are of the WHOLE run, not of the kept rows, so a truncated table still reads honestly: three rows
// summing to 40% says the other 60% is in the tail rather than pretending the tail is not there.
//
// Ties break on the symbol name so the same input always gives the same table -- a profile that
// reorders between runs cannot be diffed.
std::vector<SampleFunc> RankSamples(const std::vector<SampleHit> &hits, std::size_t top);

// Only the hits from one module, for the "what in OUR code is hot" question. Matched
// case-insensitively: Windows hands module names back in whatever case the loader recorded.
std::vector<SampleHit> OnlyFrom(const std::vector<SampleHit> &hits, const std::string &dso);

// The report, in the shape fuactl already expects from the mac and Linux samplers, so a caller does
// not care which one answered.
std::string SampleReportJson(const std::vector<SampleFunc> &funcs, double seconds,
	unsigned long long total);

} // namespace zx

#endif
