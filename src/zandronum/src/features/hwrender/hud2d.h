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

#include "basictypes.h"   // [rc4l] fixed_t
#include "features/draw2d/computation/quad2d_compute.h"   // [rc4l] Quad2D, the record draw2d works out

namespace zx { namespace hwrender {


// [rc4l] A 2D draw for both windows, GL's alone (the backend skips it) or the backend's alone (GL draws it with colour writes off).
enum { k2DBoth = 0, k2DGLOnly = 1, k2DBackendOnly = 2 };
void Set2DTarget(int target);
int  Get2DTarget();

// [rc4l] vid_fps per renderer: GL's frame runs from D_Display's start to the backend's LiveFrame, the backend's from there until it has drawn.
void FrameTimingBegin();
void GLFrameDone();
void BackendFrameDone();
// [rc4l] Whether the backend drew within the last second, and a renderer's last whole second as vid_fps shows it.
bool BackendRateLive();
void RendererRateShown(bool backend, int &ms, int &fps);

// [rc4l] Render one camera texture: the world from another viewpoint, into the backend's own target.
//
// This is the one thing a backend cannot get by replaying what the engine captured. A camera texture
// is GL-rendered into a GL texture, which Diligent cannot read, so it has to draw the view itself.
// Same machinery a portal, a mirror or a skybox needs.
// [rc4l] w by h is the picture's size in pixels, aspect the shape GL frames it to (the texture's scaled size), which differ when ANIMDEFS fits it smaller.
void RenderCameraTexture(const void *material, int w, int h, float aspect,
                         fixed_t px, fixed_t py, fixed_t pz, unsigned int pangle, int ppitch, float fovDeg);

// [rc4l] The sky's fade layer: a translucent sheet in the sector's fade colour that GL draws over
// the sky after the dome. Set every frame the sky portal runs, zeroed when it should not be drawn.
void SetSkyFog(int r, int g, int b, float a);
void GetSkyFog(float &r, float &g, float &b, float &a);

// Drop last frame's list. Called once at the top of D_Display, before anything draws.
void Clear2D();

// Record one quad. Cheap and allocation-free after the first few frames.
void Record2D(const Quad2D &q);

// [rc4l] True while the backend is carrying the frame on its own and GL should not derive the
// scene. See fua_dg_standalone: the cvar asks for it, this says whether it is actually happening --
// the level has to have been baked first, and that bake needs GL frames to run in.
bool StandaloneActive();

// [rc4l] Is the Diligent child window what the player is looking at?
//
// Broader than StandaloneActive, and the difference matters. Standalone decides who BUILDS the
// frame; this decides who PRESENTS it. Once the embedded swapchain is up, GL's back buffer is not
// the picture no matter which path built the geometry -- so anything still drawing only into GL is
// drawing into a surface nobody sees.
bool BackendPresenting();

// [rc4l] The camera the backend is drawing with, column-major. Exposed so a standalone frame can
// cull actors against the same matrix rather than deriving the camera a second time -- two
// derivations of one camera is how a yaw error mirrored the whole world for weeks.
const float *SceneMVP();

// This frame's quads, in submission order -- which IS the draw order, because 2D is painter's
// algorithm all the way down and reordering it would put the status bar behind the world.
const Quad2D *Quads2D(int &count);

// Bumped by every Clear2D, so a backend can upload once per frame rather than once per draw call.
unsigned int Generation2D();

// The screen the coordinates are relative to, captured when the frame's first quad is recorded.
void GetScreen2D(int &w, int &h);

}} // namespace zx::hwrender

#endif // ZX_HUD2D_H
