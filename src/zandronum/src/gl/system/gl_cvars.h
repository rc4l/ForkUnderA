

#ifndef _GL_INTERN_H
#define _GL_INTERN_H

#include "r_defs.h"
#include "c_cvars.h"

#ifdef _MSC_VER
#pragma warning(disable:4244)
#endif

EXTERN_CVAR(Bool,r_enhanced_nightvision)
EXTERN_CVAR(Int, screenblocks);
EXTERN_CVAR(Bool, gl_texture)
EXTERN_CVAR(Int, r_texture_filter)
EXTERN_CVAR(Float, r_texture_filter_anisotropic)
EXTERN_CVAR(Int, gl_texture_format)
EXTERN_CVAR(Bool, r_texture_usehires)
EXTERN_CVAR(Bool, gl_usefb)

EXTERN_CVAR(Int, r_weaponlight)

EXTERN_CVAR (Bool, r_lights);
EXTERN_CVAR (Bool, r_attachedlights);
EXTERN_CVAR (Bool, r_lights_checkside);
EXTERN_CVAR (Float, r_lights_intensity);
EXTERN_CVAR (Float, r_lights_size);
EXTERN_CVAR (Bool, r_lights_additive);
EXTERN_CVAR (Bool, r_light_sprites);
EXTERN_CVAR (Bool, r_light_particles);

EXTERN_CVAR(Int, r_fogmode)
EXTERN_CVAR(Int, r_lightmode)
EXTERN_CVAR(Bool,r_mirror_envmap)

EXTERN_CVAR(Bool,r_mirrors)
EXTERN_CVAR(Bool,r_mirror_envmap)
EXTERN_CVAR(Bool, r_seamless)

EXTERN_CVAR(Float, gl_mask_threshold)
EXTERN_CVAR(Float, r_mask_sprite_threshold)

EXTERN_CVAR(Int, gl_usevbo)



EXTERN_CVAR(Int, gl_usevbo)

// [AK] Override gl_texture in the same way as r_fogmode.
#define OVERRIDE_GL_TEXTURE_IF_NECESSARY \
	const bool gl_texture_CVAR_value = !!gl_texture; \
	const bool gl_texture = ( zadmflags & ZADF_FORCE_VIDEO_DEFAULTS ) || gl_texture_CVAR_value;

#endif // _GL_INTERN_H
