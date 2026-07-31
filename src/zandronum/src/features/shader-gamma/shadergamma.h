// [rc4l] Shader gamma: applies Gamma/vid_contrast/vid_brightness while presenting the scene
// texture to the backbuffer, replacing the OS hardware gamma ramp.
//
// See README.md in this directory for why the hardware ramp had to go (it programs the DISPLAY's
// LUT, so it leaked onto the desktop on alt-tab).
//
// Deliberately self-contained rather than a port of upstream's FShaderProgram / FGLRenderBuffers /
// hw_postprocess stack: that machinery exists to host bloom, SSAO, tonemapping and a uniform-block
// system we have none of, and the seam catalog's standing lesson is not to adopt upstream's
// frame-loop infrastructure wholesale. This is one program, one quad, three uniforms.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_SHADERGAMMA_H
#define ZX_SHADERGAMMA_H

namespace zx
{

// [rc4l] Compile the present program and build its quad. Safe to call repeatedly; a second call is
// a no-op. Returns false if the shader would not compile or link, in which case the caller must
// fall back to a plain blit -- a failed present must never mean a black screen.
bool ShaderGammaInit();

// [rc4l] Release the program/quad (GL context teardown).
void ShaderGammaShutdown();

// [rc4l] True once Init has succeeded, i.e. the shader present path is usable.
bool ShaderGammaReady();

// [rc4l] Draw `sceneTexture` over the currently-bound draw framebuffer, applying the gamma
// uniforms derived from the cvars. `destW`/`destH` set the viewport.
void ShaderGammaPresent(unsigned int sceneTexture, int destW, int destH);

} // namespace zx

#endif // ZX_SHADERGAMMA_H
