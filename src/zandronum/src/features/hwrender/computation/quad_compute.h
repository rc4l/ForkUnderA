// [rc4l] Pure 2D quad tessellation into two CCW triangles, replacing glBegin(GL_QUADS) HUD/2D draws.
#ifndef ZX_HWRENDER_QUAD_COMPUTE_H
#define ZX_HWRENDER_QUAD_COMPUTE_H

namespace hwrender
{

// [rc4l] Interleaved 2D vertex: position (z kept for a shared 3D pipeline), UV, RGBA8 color.
struct Vertex2D
{
	float x, y, z;
	float u, v;
	unsigned char r, g, b, a;
};

// [rc4l] Fill out[6] with two CCW triangles covering the rect [x,x+w] x [y,y+h], textured with [u0,u1] x [v0,v1] and tinted (r,g,b,a). Replaces a glBegin(GL_QUADS)/glTexCoord/glVertex block.
void ComputeQuadVertices(float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1,
                         unsigned char r, unsigned char g, unsigned char b, unsigned char a,
                         Vertex2D out[6]);

} // namespace hwrender

#endif // ZX_HWRENDER_QUAD_COMPUTE_H
