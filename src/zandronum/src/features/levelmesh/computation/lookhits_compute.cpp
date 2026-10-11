// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "lookhits_compute.h"

namespace zx { namespace levelmesh {

int ComputeKeepNearestHit(float *ts, int *ids, int count, int cap, float t, int id)
{
	// [rc4l] A piece already kept moves only nearer, so its other triangles cannot list it twice.
	for (int i = 0; i < count; i++)
	{
		if (ids[i] != id) continue;
		if (t >= ts[i]) return count;
		for (int k = i; k + 1 < count; k++) { ts[k] = ts[k + 1]; ids[k] = ids[k + 1]; }
		count--;
		break;
	}
	int at = count;
	while (at > 0 && ts[at - 1] > t) at--;
	if (at >= cap) return count;
	const int kept = (count < cap) ? count + 1 : cap;
	for (int k = kept - 1; k > at; k--) { ts[k] = ts[k - 1]; ids[k] = ids[k - 1]; }
	ts[at] = t;
	ids[at] = id;
	return kept;
}

}} // namespace zx::levelmesh
