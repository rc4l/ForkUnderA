// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The on-screen stick as a joystick the engine already knows: an IJoystickConfig "Touch" with the joystick menu's dead zone, sensitivity and axis map, summed into I_GetAxes beside the real ones.

#ifndef ZX_TOUCHJOYSTICK_H
#define ZX_TOUCHJOYSTICK_H

#include "m_joy.h"

namespace zx { namespace touch {

// [rc4l] The stick's deflection this tic, x right and y down in -1..1 as a joystick's axes read.
void SetStick(float x, float y);

// [rc4l] Adds the stick to the game axes, as a joystick's AddAxes does.
void AddAxes(float axes[NUM_JOYAXIS]);

// [rc4l] The device for I_GetJoysticks and the joystick menu, made on first ask; NULL until a touch screen has been seen.
IJoystickConfig *Device();

// [rc4l] Marks that the host has a touch screen, so the device appears in the joystick menu.
void NoteTouchSeen();

}} // namespace zx::touch

#endif // ZX_TOUCHJOYSTICK_H
