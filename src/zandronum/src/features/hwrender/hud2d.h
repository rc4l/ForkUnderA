// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The engine's 2D layer, captured as a per-frame quad list.
//
// Everything the player reads is here: status bar, weapon sprite, messages, the automap overlay, the
// menus, the console. Without it a backend renders a world with nothing in front of it, which is a
// tech demo rather than a game.
//
// The engine draws 2D through FGLRenderer::DrawTexture -- one call per texture, and text is one call
// per character -- straight into GL immediate-ish state. Rather than reimplement that path, this
// records what each call *would* draw as a flat list of screen-space quads. Same relationship the
// level mesh has to the 3D path: the backend consumes plain data and never sees a DCanvas::DrawParms.
//
// Rebuilt from scratch every frame. 2D is entirely view-dependent (the HUD moves, text scrolls,
// menus open) so there is nothing to cache, and a list that is cleared and refilled cannot go stale
// the way the first sprite attempt did.
//
// Deliberately Diligent-free: this header is included by the GL renderer, and every Diligent header
// drags in a reshaped windows.h that fights with the engine's own.

#ifndef ZX_HUD2D_H
#define ZX_HUD2D_H

namespace zx { namespace hwrender {

// One screen-space textured quad. Coordinates are in the engine's 2D space -- ortho(0, W, H, 0),
// origin top-left, Y down -- exactly as OpenGLFrameBuffer::Begin2D sets it up.
struct Quad2D
{
	const void *material;              // FMaterial*, identity only; NULL means an untextured fill
	float       x, y, w, h;            // destination rect, screen pixels
	float       u1, v1, u2, v2;        // source rect, already flipped/windowed by the caller
	float       r, g, b, a;            // colour modulation, 0..1
	int         clipL, clipT, clipR, clipB;   // scissor, screen pixels; clipR <= clipL means none
	int         blend;                 // 0 normal alpha, 1 additive
	// [rc4l] Palette remap index, in FMaterial::CreateTexBuffer's convention. Coloured text is the
	// same glyph under a different translation; ignoring it renders every font in the base palette.
	int         translation;
	// [rc4l] TexMode from gl_interface.h. Coloured text is drawn with TM_MASK, where the texture is
	// only a coverage mask and the vertex colour supplies RGB -- multiplying by the texel instead
	// renders the whole font muddy and dark.
	int         texMode;
	// [rc4l] What this record IS: 0 a textured/solid rect, 1 a line from (x,y) to (lx2,ly2).
	//
	// The automap is drawn with GL_LINES through FGLRenderer::DrawLine, not with DrawTexture, so a
	// capture that only understood rects recorded the background and none of the map -- the automap
	// came up as a flat fill. A line cannot be expressed as an axis-aligned rect because it is
	// diagonal, and it cannot go in a separate list either: 2D is painter's algorithm, and lines
	// interleave with the text drawn over them.
	int         kind;
	float       lx2, ly2;
};

// [rc4l] Render one camera texture: the world from another viewpoint, into the backend's own target.
//
// This is the one thing a backend cannot get by replaying what the engine captured. A camera texture
// is GL-rendered into a GL texture, which Diligent cannot read, so it has to draw the view itself.
// Same machinery a portal, a mirror or a skybox needs.
void RenderCameraTexture(const void *material, int w, int h,
                         int px, int py, int pz, unsigned int pangle, int ppitch, float fovDeg);

// [rc4l] The sky's fade layer: a translucent sheet in the sector's fade colour that GL draws over
// the sky after the dome. Set every frame the sky portal runs, zeroed when it should not be drawn.
void SetSkyFog(int r, int g, int b, float a);
void GetSkyFog(float &r, float &g, float &b, float &a);

// Drop last frame's list. Called once at the top of D_Display, before anything draws.
void Clear2D();

// Record one quad. Cheap and allocation-free after the first few frames.
void Record2D(const Quad2D &q);

// This frame's quads, in submission order -- which IS the draw order, because 2D is painter's
// algorithm all the way down and reordering it would put the status bar behind the world.
const Quad2D *Quads2D(int &count);

// Bumped by every Clear2D, so a backend can upload once per frame rather than once per draw call.
unsigned int Generation2D();

// The screen the coordinates are relative to, captured when the frame's first quad is recorded.
void GetScreen2D(int &w, int &h);

}} // namespace zx::hwrender

#endif // ZX_HUD2D_H
