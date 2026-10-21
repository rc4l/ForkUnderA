// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/touch-input/touchjoystick.h"

#include "zstring.h"

namespace zx { namespace touch {

namespace {

// [rc4l] The two axes the stick has, named as the joystick menu shows them.
const int kAxes = 2;
const EJoyAxis kDefaultMap[kAxes] = { JOYAXIS_Side, JOYAXIS_Forward };
const char *const kAxisNames[kAxes] = { "Stick X", "Stick Y" };
const float kDefaultDeadZone = 0.f;   // [rc4l] The screen stick has its own radial dead zone (FUATOUCH), so the joystick's is off by default.

class TouchJoystick : public IJoystickConfig
{
public:
	TouchJoystick() { SetDefaultConfig(); M_LoadJoystickConfig(this); }
	~TouchJoystick() { M_SaveJoystickConfig(this); }

	FString GetName() { return "Touch"; }
	float GetSensitivity() { return mSensitivity; }
	void SetSensitivity(float scale) { mSensitivity = scale; }

	int GetNumAxes() { return kAxes; }
	float GetAxisDeadZone(int axis) { return mDeadZone[axis]; }
	EJoyAxis GetAxisMap(int axis) { return mMap[axis]; }
	const char *GetAxisName(int axis) { return kAxisNames[axis]; }
	float GetAxisScale(int axis) { return mScale[axis]; }

	void SetAxisDeadZone(int axis, float zone) { mDeadZone[axis] = zone < 0.f ? 0.f : zone > 1.f ? 1.f : zone; }
	void SetAxisMap(int axis, EJoyAxis gameaxis) { mMap[axis] = gameaxis; }
	void SetAxisScale(int axis, float scale) { mScale[axis] = scale; }

	bool IsSensitivityDefault() { return mSensitivity == 1.f; }
	bool IsAxisDeadZoneDefault(int axis) { return mDeadZone[axis] == kDefaultDeadZone; }
	bool IsAxisMapDefault(int axis) { return mMap[axis] == kDefaultMap[axis]; }
	bool IsAxisScaleDefault(int axis) { return mScale[axis] == 1.f; }

	void SetDefaultConfig()
	{
		mSensitivity = 1.f;
		for (int i = 0; i < kAxes; i++) { mDeadZone[i] = kDefaultDeadZone; mMap[i] = kDefaultMap[i]; mScale[i] = 1.f; }
	}
	FString GetIdentifier() { return "touch"; }

	// [rc4l] As SDLInputJoystick::AddAxes: a joystick's dead zone again (off unless the player sets one), the value taken from the game axis so right reads as strafe right and up as forward.
	void AddAxes(float axes[NUM_JOYAXIS], float x, float y)
	{
		const float raw[kAxes] = { x, y };
		for (int i = 0; i < kAxes; i++)
		{
			if (mMap[i] == JOYAXIS_None) continue;
			BYTE unused = 0;   // [rc4l] Joy_RemoveDeadZone writes the button it would press through this.
			const double value = Joy_RemoveDeadZone(raw[i], mDeadZone[i], &unused);
			axes[mMap[i]] -= float(value * mSensitivity * mScale[i]);
		}
	}

private:
	float mSensitivity;
	float mDeadZone[kAxes];
	EJoyAxis mMap[kAxes];
	float mScale[kAxes];
};

TouchJoystick *g_device = NULL;
bool g_seen = false;
float g_x = 0.f, g_y = 0.f;

} // namespace

void SetStick(float x, float y) { g_x = x; g_y = y; }

void NoteTouchSeen() { g_seen = true; }

IJoystickConfig *Device()
{
	if (!g_seen) return NULL;
	if (g_device == NULL) g_device = new TouchJoystick();
	return g_device;
}

void AddAxes(float axes[NUM_JOYAXIS])
{
	if (g_x == 0.f && g_y == 0.f) return;
	TouchJoystick *const device = static_cast<TouchJoystick *>(Device());
	if (device != NULL) device->AddAxes(axes, g_x, g_y);
}

}} // namespace zx::touch
