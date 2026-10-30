// [Doruk-Aksoy] Where a run of quads lands at the end of the dynamic stream.
#include "features/levelmesh/computation/dynquads_compute.h"

namespace zx { namespace levelmesh {

bool ComputeDynQuadRun(unsigned verts, unsigned pieces, unsigned count, unsigned ceiling, DynQuadRun &run)
{
	// [Doruk-Aksoy] Compared without adding to verts, so a stream near the top of the range cannot wrap past the ceiling.
	if (verts > ceiling || count > (ceiling - verts) / 4) return false;
	run.firstPiece = pieces;
	run.firstVert = verts;
	return true;
}

unsigned ComputeDynQuadVertex(const DynQuadRun &run, unsigned k)
{
	return run.firstVert + 4 * k;
}

}} // namespace zx::levelmesh
