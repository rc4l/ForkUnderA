// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The dynamic light settings both renderers read, moved from gl/dynlights/gl_dynlight1.cpp.

#include "c_cvars.h"
#include "features/gldefs/gldefs.h"   // [rc4l] the attached lights

CUSTOM_CVAR (Bool, gl_lights, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (self) gl_RecreateAllAttachedLights();
	else gl_DeleteAllAttachedLights();
}

CVAR (Bool, gl_attachedlights, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
CVAR (Bool, gl_lights_checkside, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
CVAR (Float, gl_lights_intensity, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
CVAR (Float, gl_lights_size, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
CVAR (Bool, gl_light_sprites, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
CVAR (Bool, gl_light_particles, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
CUSTOM_CVAR (Bool, gl_lights_additive, false,  CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	gl_DeleteAllAttachedLights();
	gl_RecreateAllAttachedLights();
}
