// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The arithmetic of a touch screen's stick and look drag, engine-free: pixels of thumb into joystick axes and mouse motion.

#ifndef ZX_TOUCHINPUT_COMPUTE_H
#define ZX_TOUCHINPUT_COMPUTE_H

namespace zx { namespace touch {

// [rc4l] A stick's deflection as a joystick reports it: x right and y down, each -1..1, as SDL's axes read before the game negates them.
struct TouchAxes { float x, y; };

// [rc4l] The thumb's offset from where it landed (pixels), over the stick's radius, with a radial dead zone (a fraction of the radius) made the new zero and the rim the limit.
TouchAxes ComputeTouchAxes(float dx, float dy, float radius, float deadZone);

// [rc4l] A look drag's pixels as the mouse motion PostMouseMove takes (x right, y up), scaled, the vertical inverted on request.
void ComputeTouchLook(float dx, float dy, float scale, bool invertY, int &mouseX, int &mouseY);

}} // namespace zx::touch

#endif // ZX_TOUCHINPUT_COMPUTE_H
