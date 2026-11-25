// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] A model surface drawn from geometry resident on the GPU: its geometry's words, its record in the dynamic stream, the reference its corners carry, and the corner the vertex shader makes of them.
#ifndef ZX_MODELGPU_COMPUTE_H
#define ZX_MODELGPU_COMPUTE_H

namespace zx { namespace levelmesh {

// [Doruk-Aksoy] Whether a model surface goes to the GPU: the switch is on, the device takes it, the surface is only drawn (not compared, which needs its corners), it is no flat surface (whose corners the backend reads) and its geometry was not refused (state 2, kModelGpuRefused).
bool ComputeGpuModelWanted(bool enabled, bool supported, bool drawnOnly, bool flatSurface, int gpuState);

// [Doruk-Aksoy] Words a resident vertex takes, FModelVertex's x, y, z, u, v.
const unsigned kModelVertexWords = 5;

// [Doruk-Aksoy] Words a model surface's record takes in the dynamic stream.
const unsigned kModelRecordWords = 8;

// [Doruk-Aksoy] The bit a model corner's reference carries, under the packed sprite's top bit and over every general vertex's index.
const unsigned kModelRefFlag = 0x40000000u;

// [Doruk-Aksoy] The bit a record's index word carries when its surface draws consecutive vertices (MD2) rather than by index.
const unsigned kModelSequential = 0x80000000u;

// [Doruk-Aksoy] The resident buffer's words when `need` are wanted: half as many again and never under 1 M words (4 MB), held to the `maxWords` the device binds; 0 when need itself is past them.
unsigned ComputeModelGeoCapacity(unsigned long long need, unsigned long long maxWords);

// [Doruk-Aksoy] Where a geometry's words begin in the resident buffer: its vertices', then its indices'.
struct ModelGeoPlace
{
	unsigned vertWord, indexWord;
};

// [Doruk-Aksoy] Words a geometry of `nverts` vertices and `nindices` indices takes resident.
unsigned ComputeModelGeoWords(unsigned nverts, unsigned nindices);

// [Doruk-Aksoy] A geometry's words written to out, its vertices (FModelVertex, five floats each) then its indices, for a buffer where out is word `at`; where each begins.
ModelGeoPlace ComputeModelGeoPack(const float *verts, unsigned nverts, const unsigned *indices, unsigned nindices, unsigned at, unsigned *out);

// [Doruk-Aksoy] What a model surface's record holds: its run's first vertex in the stream, its piece record, its geometry's place, its span (ModelSurfaceSpan) and its matrix's first word.
struct ModelRecordIn
{
	unsigned firstRef, piece;
	ModelGeoPlace place;
	bool indexed;
	unsigned base1, base2, firstIndex;
	float inter;
	unsigned matrixWord;
};

// [Doruk-Aksoy] The record's words: first vertex, piece, both keyframes' first words, the lerp (none unless the keyframes differ and it is above 0, as ComputeModelSurfaceCorners lerps), the index word, the matrix word.
void ComputeModelRecord(const ModelRecordIn &in, unsigned out[kModelRecordWords]);

// [Doruk-Aksoy] The reference every corner of the surface whose record is at `recordWord` carries.
unsigned ComputeModelRef(unsigned recordWord);

// [Doruk-Aksoy] The dynamic vertex shader's model branch, word for word: the corner `vertexIndex` (gl_VertexIndex) of reference `ref` as MeshVertex holds it (x, height, y, u, v), every read held inside its buffer as the shader holds it; false when ref is no model's.
bool ComputeModelCorner(const unsigned *dynWords, unsigned ndyn, const unsigned *geoWords, unsigned ngeo, unsigned ref, unsigned vertexIndex, float out[5]);

// [Doruk-Aksoy] The box (min x, y, z, then max) of a span's corners in model space, read as ComputeModelSurfaceCorners reads them from keyframe `base`; false when the span reads outside or has no corner.
bool ComputeModelSpanBox(const float *verts, unsigned nverts, const unsigned *indices, unsigned nindices, unsigned base, unsigned firstIndex, unsigned count, float box[6]);

// [Doruk-Aksoy] The world box (min, then max, in MeshVertex order) holding both keyframes' boxes under the column-major matrix m, which holds every lerp between them.
void ComputeModelBoxUnder(const float a[6], const float b[6], const float *m, float out[6]);

}} // namespace zx::levelmesh

#endif
