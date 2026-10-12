// [rc4l] Models, resolved on the CPU and handed to the dynamic mesh.
//
// A model is drawn by GL as two frames of vertices plus an interpolation factor, lerped in the
// vertex shader, under a per-object matrix. None of that survives the trip to a backend that has
// neither an interpolation factor nor a per-piece transform. So the frame is resolved here instead:
// lerp the two frames, apply the matrix, and append world-space triangles to the same dynamic
// stream sprites and decals use -- which already carries material, translation, blend mode and the
// CPU-folded lighting, and already sorts back to front.
//
// This is Option A in docs/models-scope.md. Option B (port the model renderer, keep the lerp on the
// GPU) is the better end state and a second draw path; this is the one that makes models visible.

#ifndef ZX_LEVELMESH_MODELMESH_H
#define ZX_LEVELMESH_MODELMESH_H

#include "features/levelmesh/staticmesh.h"   // MeshPiece

class GLSprite;
class FMaterial;
class FModelVertexBuffer;
class VSMatrix;
struct FColormap;

namespace zx { namespace levelmesh {

// [rc4l] The look a model's surfaces share, the same for GL's capture and the derived model: light, colour, the actor's dynamic light, translucency and sort point.
void ModelPieceLook(int lightLevel, int extraLight, const FColormap &cm, bool fullbright, const float dyn[3], unsigned thingColor,
	float alpha, int blendMode, float x, float y, float z, MeshPiece &mp);

// Bracket one actor's model draw. Between these the surfaces know which actor they belong to, which
// is where their lighting, translucency and sort position come from.
void ModelCaptureBegin(const GLSprite *spr, const VSMatrix &modelToWorld);
// [rc4l] The same for a model features/sprites derived: its look, its column-major model-to-world matrix and its actor; `checkOnly` notes its surfaces for fua_sprite_check instead of drawing them.
void ModelCaptureBeginDerived(const MeshPiece &look, const float *modelToWorld, const void *who, bool checkOnly);
void ModelCaptureEnd();

// True when a backend is carrying the frame and a surface should be captured rather than rasterised.
bool ModelCaptureActive();

// One surface: an index range into the model's own buffers, the two frames to lerp between, and the
// skin's material to draw it with. base1/base2 are vertex offsets, already including the frame stride.
void CaptureModelSurface(const FModelVertexBuffer *vbuf, unsigned int base1, unsigned int base2,
	float inter, unsigned int firstIndex, unsigned int indexCount,
	const void *material, int translation);

// Per-frame counters, for fua_models.
void ModelStatsReset();
void ModelStatsGet(int &models, int &surfaces, int &triangles, int &skipped);

// The world-space box the captured triangles occupy, and the last actor they came from. A count
// proves the capture ran; this says whether what it produced is anywhere near the camera.
void ModelBoundsGet(float *mn, float *mx, float *lastSort);
void ModelPieceGet(float &alpha, int &blend, int &light, float *rgb, const void *&material);

}} // namespace zx::levelmesh

#endif
