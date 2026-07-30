// [MGOOOOOO] See boxedges_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#include "features/hitboxviz/computation/boxedges_compute.h"

namespace zx { namespace hitboxviz {

namespace
{
	// The four horizontal corners, walked as a loop so consecutive entries share an edge.
	const float CORNER_X[4] = { -1.f,  1.f, 1.f, -1.f };
	const float CORNER_Y[4] = { -1.f, -1.f, 1.f,  1.f };

	inline void Emit(Vertex3 *&out, float x, float y, float z)
	{
		out->x = x;
		out->y = y;
		out->z = z;
		++out;
	}
}

unsigned int BuildBoxEdges(float centerX, float centerY, float bottomZ, float radius, float height, Vertex3 *out)
{
	if (radius <= 0.f || height <= 0.f)
		return 0;

	const float topZ = bottomZ + height;
	Vertex3 *const start = out;

	for (int i = 0; i < 4; ++i)
	{
		const int next = (i + 1) & 3;

		const float x0 = centerX + radius * CORNER_X[i];
		const float y0 = centerY + radius * CORNER_Y[i];
		const float x1 = centerX + radius * CORNER_X[next];
		const float y1 = centerY + radius * CORNER_Y[next];

		// bottom face
		Emit(out, x0, y0, bottomZ);
		Emit(out, x1, y1, bottomZ);
		// top face
		Emit(out, x0, y0, topZ);
		Emit(out, x1, y1, topZ);
		// corner post joining the two
		Emit(out, x0, y0, bottomZ);
		Emit(out, x0, y0, topZ);
	}

	return static_cast<unsigned int>(out - start);
}

unsigned int BuildPlaneMarker(float centerX, float centerY, float planeZ, float radius, Vertex3 *out)
{
	if (radius <= 0.f)
		return 0;

	Vertex3 *const start = out;

	Emit(out, centerX - radius, centerY - radius, planeZ);
	Emit(out, centerX + radius, centerY + radius, planeZ);
	Emit(out, centerX - radius, centerY + radius, planeZ);
	Emit(out, centerX + radius, centerY - radius, planeZ);

	return static_cast<unsigned int>(out - start);
}

BlastPrism ComputeBlastPrism(float x, float y, float z, float distance)
{
	BlastPrism prism;
	prism.centerX = x;
	prism.centerY = y;
	prism.bottomZ = z - distance;
	prism.radius  = distance;
	prism.height  = distance * 2.f;
	return prism;
}

int ClampFullDamageDistance(int distance, int fulldamagedistance)
{
	// Matches p_map.cpp: clamp<int>(fulldamagedistance, 0, bombdistance - 1). A non-positive
	// distance means there is no blast at all, so there is no inner region either.
	if (distance <= 0)
		return 0;
	if (fulldamagedistance < 0)
		return 0;
	if (fulldamagedistance > distance - 1)
		return distance - 1;
	return fulldamagedistance;
}

}} // namespace zx::hitboxviz
