// [rc4l] A model's geometry on the CPU, which GL uploads and the backend resolves frames from: every keyframe's vertices back to back, and the indices an indexed format draws them by.

#ifndef ZX_MODELGEOMETRY_H
#define ZX_MODELGEOMETRY_H

#include "tarray.h"

struct FModelVertex
{
	float x, y, z;	// world position
	float u, v;		// texture coordinates

	void Set(float xx, float yy, float zz, float uu, float vv)
	{
		x = xx;
		y = yy;
		z = zz;
		u = uu;
		v = vv;
	}

	void SetNormal(float nx, float ny, float nz)
	{
		// GZDoom currently doesn't use normals. This function is so that the high level code can pretend it does.
	}
};

// [rc4l] `indexed` says the surfaces are drawn by `indices` (MD3, voxels) rather than as consecutive vertices (MD2); `built` that the loader has filled it.
struct FModelGeometry
{
	TArray<FModelVertex> verts;
	TArray<unsigned int> indices;
	bool indexed;
	bool built;

	FModelGeometry() : indexed(false), built(false) {}
};

#endif // ZX_MODELGEOMETRY_H
