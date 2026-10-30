// [Doruk-Aksoy] The screen rectangle the deferred decal pass draws each mark over: the cube round everything it can paint, cut at the near plane, worked out on the CPU and handed to its vertex stage.
#ifndef ZX_DECALBOUNDS_COMPUTE_H
#define ZX_DECALBOUNDS_COMPUTE_H

namespace zx { namespace hwrender {

// [Doruk-Aksoy] A rectangle in normalised device coordinates, or the whole screen.
struct DecalScreenRect
{
	bool whole;
	float lo[2];
	float hi[2];
};

// [Doruk-Aksoy] A mark's height after riding its sector plane (A, B, D and 1/C of Ax + By + Cz + D = 0, read at its mesh x and z), `offset` above it.
float ComputeDecalRiddenHeight(const float centre[3], const float plane[4], float offset);

// [Doruk-Aksoy] The pixels a rectangle covers on a width by height target.
double ComputeDecalRectPixels(const DecalScreenRect &r, float width, float height);

// [Doruk-Aksoy] The radius every pixel a mark paints lies within (params: near, far, run-out radius; axes divided by the picture's half-extents): the run-out radius when the run-out reaches past the picture's corners, as the shader then discards beyond it, else the reach of its box.
float ComputeDecalPaintRadius(const float params[4], const float axisU[3], const float axisV[3]);

// [Doruk-Aksoy] The eye a column-major inverse view-projection looks from, false for a projection without one.
bool ComputeDecalEye(const float *invMVP, float eye[3]);

// [Doruk-Aksoy] The rectangle of the cube of half-size radius round a mark's centre, cut at the near plane (clip z 0): nothing for a mark wholly behind it, the part in front for one across it, and the whole screen with the eye inside the cube.
DecalScreenRect ComputeDecalClippedRect(const float centre[3], float radius, const float eye[3], const float *mvp);

// [Doruk-Aksoy] Whether a rectangle covers any of the screen.
bool ComputeDecalRectShows(const DecalScreenRect &r);

}} // namespace zx::hwrender

#endif
