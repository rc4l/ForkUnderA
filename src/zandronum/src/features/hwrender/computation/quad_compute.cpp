// [rc4l] Implementation of 2D quad tessellation. No engine deps.
#include "features/hwrender/computation/quad_compute.h"

namespace hwrender
{

void ComputeQuadVertices(float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1,
                         unsigned char r, unsigned char g, unsigned char b, unsigned char a,
                         Vertex2D out[6])
{
	const float x1 = x + w;
	const float y1 = y + h;

	// [rc4l] Corners: top-left, bottom-left, bottom-right, top-right.
	const Vertex2D tl{x,  y,  0.0f, u0, v0, r, g, b, a};
	const Vertex2D bl{x,  y1, 0.0f, u0, v1, r, g, b, a};
	const Vertex2D br{x1, y1, 0.0f, u1, v1, r, g, b, a};
	const Vertex2D tr{x1, y,  0.0f, u1, v0, r, g, b, a};

	// [rc4l] Two CCW triangles: (tl, bl, br) and (tl, br, tr).
	out[0] = tl; out[1] = bl; out[2] = br;
	out[3] = tl; out[4] = br; out[5] = tr;
}

} // namespace hwrender
