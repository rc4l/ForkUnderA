// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A piece's record as the scene shaders read it, and a plain sprite's packed into a third of it with its four corners, so a frame's thousands of sprites send 112 bytes each instead of a record and six vertices.

#ifndef ZX_SPRITEPACK_COMPUTE_H
#define ZX_SPRITEPACK_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] What a piece knows about itself, once rather than once per vertex, in vec4s since std430 pads a vec3 to 16 bytes anyway.
struct ScenePieceData
{
	float r, g, b, softLight;
	float fogDensity, alpha, fogMode, matSlot;
	float fogR, fogG, fogB, warpStyle;   // [rc4l] ANIMDEFS warp (1) or warp2 (2), else 0
	float nx, ny, nz, warpSpeed;
	// [rc4l] A flooded gap: u = floodU.xy . (x, y) + floodU.z on the plane at height floodU.w, v likewise, flood 1 to switch it on.
	float floodUx, floodUy, floodU0, floodZ;
	float floodVx, floodVy, floodV0, flood;
	// [rc4l] A sprite's or model's light from the CPU (GL's gl_SetDynSpriteLight), cpuLight 1 to use it instead of the shader's loop.
	float actorR, actorG, actorB, cpuLight;
	// [rc4l] The slot holding this material's brightmap in the bindless array (0 for none), the piece's PieceStackId plus one (0 for a sprite or model, never culled), 1 when both slots go through SlotRemap, and 1 when GL draws it unmasked so no texel is discarded.
	float brightSlot, stackId, animates, plain;
	// [rc4l] A wall's glow (MeshPiece::glowTop and the rest): two colours with their heights, and the ceiling and floor planes they are measured from.
	float glowTop[4], glowBottom[4], glowTopPlane[4], glowBottomPlane[4];
	// [rc4l] A player's damage tint (MeshPiece::tint): colour and strength, then its texture-V span and 1 when it glows.
	float tint[4], tintRange[4];
	float desaturation, scrollU, scrollV, texFlags;   // [rc4l] the colormap's desaturation, 0..1, which main.fp mixes every texel toward grey by, the texture shift a scroller added since the bake, and the piece's texture modes, styles and passes (ComputePieceTexFlags)
};

// [rc4l] One corner of a quad, its position and texture coordinate.
struct SpriteCorner { float x, y, z, u, v; };

// [rc4l] The 32-bit words a packed sprite takes, a multiple of four so each starts on a word offset whose low two bits can carry a corner.
enum { kSpriteWords = 28 };

// [rc4l] Packs a quad's record and corners into out, false when the record holds anything the packed form cannot carry bit for bit: a warp, a normal, a flood, a glow, a tint, a scroll, a stacked view, an animated slot, a plain draw, a colour off an 8-bit step, texture coordinates that are no rectangle, or a slot or texture flag past 16 bits.
bool ComputeSpritePack(const ScenePieceData &pd, const SpriteCorner corners[4], unsigned out[kSpriteWords]);

// [rc4l] The record and the corner'th corner a packed sprite holds, as the dynamic scene shader reads them (dgscene_glsl.h kDynamicVS).
void ComputeSpriteUnpack(const unsigned in[kSpriteWords], int corner, ScenePieceData &pd, SpriteCorner &c);

// [rc4l] A vertex of the dynamic stream: a corner of the packed sprite at wordOffset (a multiple of four), else the general vertex at index; the shader tells them apart by the top bit.
unsigned ComputeSpriteRef(unsigned wordOffset, int corner);
unsigned ComputeGeneralRef(unsigned index);

}} // namespace zx::hwrender

#endif // ZX_SPRITEPACK_COMPUTE_H
