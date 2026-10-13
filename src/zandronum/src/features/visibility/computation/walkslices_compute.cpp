// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/visibility/computation/walkslices_compute.h"

#include <algorithm>

namespace zx { namespace visibility {

void ComputeWalkSlices(unsigned viewAngle, unsigned halfAngle, int count, const double *edges, unsigned *bounds)
{
	unsigned from = 0;
	unsigned long long span = 1ull << 32;
	if (halfAngle < 0x80000000u)
	{
		from = ComputeAngleToPseudo(viewAngle - halfAngle);
		span = (unsigned)(ComputeAngleToPseudo(viewAngle + halfAngle) - from);
	}
	for (int k = 0; k <= count; k++) bounds[k] = from + (unsigned)(unsigned long long)(double(span) * edges[k]);
}

void ComputeBalancedSlices(const double *edges, const long long *work, int count, double minGap, double *out)
{
	double total = 0;
	for (int k = 0; k < count; k++) total += double(work[k]);
	out[0] = 0;
	out[count] = 1;
	double cum = 0;
	int k = 0;
	for (int i = 1; i < count; i++)
	{
		double target = edges[i];
		if (total > 0)
		{
			const double share = total * i / count;
			while (k < count - 1 && cum + double(work[k]) < share) cum += double(work[k++]);
			const double within = work[k] > 0 ? (share - cum) / double(work[k]) : 0;
			target = edges[k] + (edges[k + 1] - edges[k]) * (within < 1 ? within : 1);
		}
		out[i] = 0.5 * (edges[i] + target);
	}
	// [rc4l] Every slice keeps some width, pushed up from the start and then down from the end.
	for (int i = 1; i < count; i++) if (out[i] < out[i - 1] + minGap) out[i] = out[i - 1] + minGap;
	for (int i = count - 1; i > 0; i--) if (out[i] > out[i + 1] - minGap) out[i] = out[i + 1] - minGap;
}

void ComputeSliceSeed(ClipRanges &clip, unsigned from, unsigned to)
{
	// [rc4l] A slice with no width blocks the whole view.
	if (from == to) clip.SafeAddClipRange(0, 0xffffffffu);
	else clip.SafeAddClipRange(to, from);
}

void ComputeLeafCounts(const CoverageNode *nodes, int count, std::vector<int> &leaves)
{
	leaves.assign(count, 0);
	if (count == 0) return;
	// [rc4l] Post-order from the root without recursion: a node is summed once both its children are.
	std::vector<int> stack(1, count - 1);
	while (!stack.empty())
	{
		const int n = stack.back();
		bool ready = true;
		for (int c = 0; c < 2; c++)
		{
			const int child = nodes[n].child[c];
			if (child >= 0 && leaves[child] == 0) { stack.push_back(child); ready = false; }
		}
		if (!ready) continue;
		stack.pop_back();
		for (int c = 0; c < 2; c++) leaves[n] += nodes[n].child[c] >= 0 ? leaves[nodes[n].child[c]] : 1;
	}
}

void ComputeLeakySegs(const int *nodeUp, const int *subUp, int subs, const int *subFirst, const int *subCount, const long long *points,
	const long long *boxes, int children, std::vector<int> &start, std::vector<int> &list)
{
	start.assign(children + 1, 0);
	list.clear();
	// [rc4l] Counted first, then placed: each seg against every box on its way up to the root.
	for (int pass = 0; pass < 2; pass++)
	{
		std::vector<int> at(start.begin(), start.end() - 1);
		if (pass == 1) list.resize(start[children]);
		for (int i = 0; i < subs; i++)
			for (int s = subFirst[i]; s < subFirst[i] + subCount[i]; s++)
			{
				const long long *p = points + s * 4;
				for (int b = subUp[i]; b >= 0; b = nodeUp[b / 2])
				{
					const long long *box = boxes + b * 4;
					bool past = false;
					for (int e = 0; e < 4; e += 2)
						past |= p[e + 1] > box[kBoxTop] || p[e + 1] < box[kBoxBottom] || p[e] < box[kBoxLeft] || p[e] > box[kBoxRight];
					if (!past) continue;
					if (pass == 0) start[b + 1]++;
					else list[at[b]++] = s;
				}
			}
		if (pass == 0)
			for (int b = 0; b < children; b++) start[b + 1] += start[b];
	}
}

bool ComputeLeakForces(const std::vector<LeakBox> &boxes, const int *nodeUp, const int *subUp, std::vector<int> &next)
{
	next.clear();
	bool matched = true;
	for (size_t i = 0; i < boxes.size(); i++)
	{
		const LeakBox &l = boxes[i];
		if (l.entered != l.forced) matched = false;
		if (!l.entered) continue;
		if (l.forced) { next.push_back(l.box); continue; }
		for (int up = subUp[l.sub]; up >= 0; up = nodeUp[up / 2])
		{
			next.push_back(up);
			if (up == l.box) break;
		}
	}
	std::sort(next.begin(), next.end());
	next.erase(std::unique(next.begin(), next.end()), next.end());
	return matched;
}

}} // namespace zx::visibility
