// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] A model surface drawn from geometry resident on the GPU.
#include "features/levelmesh/computation/modelgpu_compute.h"

#include <cstddef>
#include <cstring>

namespace zx { namespace levelmesh {

bool ComputeGpuModelWanted(bool enabled, bool supported, bool drawnOnly, bool flatSurface, int gpuState)
{
	return enabled && supported && drawnOnly && !flatSurface && gpuState != 2;
}

unsigned ComputeModelGeoWords(unsigned nverts, unsigned nindices)
{
	return nverts * kModelVertexWords + nindices;
}

unsigned ComputeModelGeoCapacity(unsigned long long need, unsigned long long maxWords)
{
	if (need > maxWords) return 0;
	unsigned long long want = need + need / 2;
	if (want < (1ull << 20)) want = 1ull << 20;
	if (want > maxWords) want = maxWords;
	return want > 0xFFFFFFFFull ? 0xFFFFFFFFu : (unsigned)want;
}

ModelGeoPlace ComputeModelGeoPack(const float *verts, unsigned nverts, const unsigned *indices, unsigned nindices, unsigned at, unsigned *out)
{
	const ModelGeoPlace place = { at, at + nverts * kModelVertexWords };
	if (nverts > 0) memcpy(out, verts, (size_t)nverts * kModelVertexWords * sizeof(unsigned));
	if (nindices > 0) memcpy(out + nverts * kModelVertexWords, indices, (size_t)nindices * sizeof(unsigned));
	return place;
}

void ComputeModelRecord(const ModelRecordIn &in, unsigned out[kModelRecordWords])
{
	const bool lerp = in.base1 != in.base2 && in.inter > 0.f;
	const float inter = lerp ? in.inter : 0.f;
	out[0] = in.firstRef;
	out[1] = in.piece;
	out[2] = in.place.vertWord + in.base1 * kModelVertexWords;
	out[3] = in.place.vertWord + in.base2 * kModelVertexWords;
	memcpy(&out[4], &inter, sizeof(inter));
	out[5] = in.indexed ? in.place.indexWord + in.firstIndex : (kModelSequential | in.firstIndex);
	out[6] = in.matrixWord;
	out[7] = 0;
}

unsigned ComputeModelRef(unsigned recordWord)
{
	return kModelRefFlag | (recordWord & (kModelRefFlag - 1));
}

// [Doruk-Aksoy] Word i of a buffer of n, held to its last as the shader holds its reads.
static unsigned Held(const unsigned *words, unsigned n, unsigned i)
{
	return n == 0 ? 0u : words[i < n ? i : n - 1];
}

static float HeldF(const unsigned *words, unsigned n, unsigned i)
{
	const unsigned w = Held(words, n, i);
	float f;
	memcpy(&f, &w, sizeof(f));
	return f;
}

bool ComputeModelCorner(const unsigned *dynWords, unsigned ndyn, const unsigned *geoWords, unsigned ngeo, unsigned ref, unsigned vertexIndex, float out[5])
{
	if ((ref & 0xC0000000u) != kModelRefFlag) return false;
	const unsigned r = ref & (kModelRefFlag - 1);
	const unsigned i = vertexIndex - Held(dynWords, ndyn, r);
	const unsigned idx = Held(dynWords, ndyn, r + 5);
	const unsigned vi = (idx & kModelSequential) != 0 ? (idx & ~kModelSequential) + i : Held(geoWords, ngeo, idx + i);
	const unsigned a = Held(dynWords, ndyn, r + 2) + vi * kModelVertexWords, b = Held(dynWords, ndyn, r + 3) + vi * kModelVertexWords;
	const float inter = HeldF(dynWords, ndyn, r + 4);
	float p[3];
	for (unsigned c = 0; c < 3; c++)
	{
		const float pa = HeldF(geoWords, ngeo, a + c);
		p[c] = pa + (HeldF(geoWords, ngeo, b + c) - pa) * inter;
	}
	const unsigned mw = Held(dynWords, ndyn, r + 6);
	float m[16];
	for (unsigned k = 0; k < 16; k++) m[k] = HeldF(dynWords, ndyn, mw + k);
	for (unsigned c = 0; c < 3; c++) out[c] = m[c] * p[0] + m[4 + c] * p[1] + m[8 + c] * p[2] + m[12 + c];
	out[3] = HeldF(geoWords, ngeo, a + 3);
	out[4] = HeldF(geoWords, ngeo, a + 4);
	return true;
}

bool ComputeModelSpanBox(const float *verts, unsigned nverts, const unsigned *indices, unsigned nindices, unsigned base, unsigned firstIndex, unsigned count, float box[6])
{
	if (verts == nullptr || count == 0) return false;
	if (indices != nullptr && (firstIndex > nindices || count > nindices - firstIndex)) return false;
	for (unsigned i = 0; i < count; i++)
	{
		const unsigned vi = indices != nullptr ? indices[firstIndex + i] : firstIndex + i;
		if (base + vi >= nverts) return false;
		const float *v = verts + kModelVertexWords * (base + vi);
		for (unsigned c = 0; c < 3; c++)
		{
			if (i == 0 || v[c] < box[c]) box[c] = v[c];
			if (i == 0 || v[c] > box[3 + c]) box[3 + c] = v[c];
		}
	}
	return true;
}

void ComputeModelBoxUnder(const float a[6], const float b[6], const float *m, float out[6])
{
	float lo[3], hi[3];
	for (unsigned c = 0; c < 3; c++)
	{
		lo[c] = a[c] < b[c] ? a[c] : b[c];
		hi[c] = a[3 + c] > b[3 + c] ? a[3 + c] : b[3 + c];
	}
	for (unsigned k = 0; k < 8; k++)
	{
		const float p[3] = { (k & 1) ? hi[0] : lo[0], (k & 2) ? hi[1] : lo[1], (k & 4) ? hi[2] : lo[2] };
		for (unsigned c = 0; c < 3; c++)
		{
			const float w = m[c] * p[0] + m[4 + c] * p[1] + m[8 + c] * p[2] + m[12 + c];
			if (k == 0 || w < out[c]) out[c] = w;
			if (k == 0 || w > out[3 + c]) out[3 + c] = w;
		}
	}
}

}} // namespace zx::levelmesh
