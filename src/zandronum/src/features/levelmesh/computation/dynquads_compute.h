// [Doruk-Aksoy] Where a run of quads lands at the end of the dynamic stream, so lanes can write each one in place while the stream keeps the order one append at a time gave.
#ifndef ZX_DYNQUADS_COMPUTE_H
#define ZX_DYNQUADS_COMPUTE_H

namespace zx { namespace levelmesh {

// [Doruk-Aksoy] The most vertices the dynamic stream holds in a frame; an append past it is dropped.
const unsigned kDynVertexCeiling = 300000;

// [Doruk-Aksoy] A run's first piece and first vertex in the stream.
struct DynQuadRun
{
	unsigned firstPiece;
	unsigned firstVert;
};

// [Doruk-Aksoy] Where `count` quads go after `verts` vertices and `pieces` pieces, false when they would pass `ceiling` vertices.
bool ComputeDynQuadRun(unsigned verts, unsigned pieces, unsigned count, unsigned ceiling, DynQuadRun &run);

// [Doruk-Aksoy] The first vertex of the run's quad `k`.
unsigned ComputeDynQuadVertex(const DynQuadRun &run, unsigned k);

}} // namespace zx::levelmesh

#endif
