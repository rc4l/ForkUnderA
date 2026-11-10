// [Doruk-Aksoy] Where a run of pieces lands at the end of the dynamic stream, so lanes can write each one in place while the stream keeps the order one append at a time gave.
#ifndef ZX_DYNQUADS_COMPUTE_H
#define ZX_DYNQUADS_COMPUTE_H

namespace zx { namespace levelmesh {

// [Doruk-Aksoy] A run's first piece and first vertex in the stream.
struct DynQuadRun
{
	unsigned firstPiece;
	unsigned firstVert;
};

// [Doruk-Aksoy] Where pieces of `addVerts` vertices in all go after `verts` vertices and `pieces` pieces, false when they would pass `ceiling` vertices.
bool ComputeDynRun(unsigned verts, unsigned pieces, unsigned addVerts, unsigned ceiling, DynQuadRun &run);

}} // namespace zx::levelmesh

#endif
