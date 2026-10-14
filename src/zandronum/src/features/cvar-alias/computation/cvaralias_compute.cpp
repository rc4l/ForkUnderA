// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/cvar-alias/computation/cvaralias_compute.h"

namespace zx { namespace cvaralias {

namespace {

struct Alias { const char *alias, *target; };

// [rc4l] One line per alias, the alias first.
const Alias kAliases[] = {
	{ "fua_vid_fps", "vid_fps" },
	// [rc4l] The settings both renderers read, renamed from GL's gl_ to r_, which old configs, binds, menus and mods still name.
	{ "gl_attachedlights", "r_attachedlights" },
	{ "gl_billboard_mode", "r_billboard_mode" },
	{ "gl_billboard_particles", "r_billboard_particles" },
	{ "gl_brightfog", "r_brightfog" },
	{ "gl_distfog", "r_distfog" },
	{ "gl_enhanced_nightvision", "r_enhanced_nightvision" },
	{ "gl_enhanced_nv_stealth", "r_enhanced_nv_stealth" },
	{ "gl_fogmode", "r_fogmode" },
	{ "gl_fuzztype", "r_fuzztype" },
	{ "gl_interpolate_model_frames", "r_interpolate_model_frames" },
	{ "gl_light_ambient", "r_light_ambient" },
	{ "gl_light_particles", "r_light_particles" },
	{ "gl_light_sprites", "r_light_sprites" },
	{ "gl_lightmode", "r_lightmode" },
	{ "gl_lights", "r_lights" },
	{ "gl_lights_additive", "r_lights_additive" },
	{ "gl_lights_checkside", "r_lights_checkside" },
	{ "gl_lights_intensity", "r_lights_intensity" },
	{ "gl_lights_size", "r_lights_size" },
	{ "gl_mask_sprite_threshold", "r_mask_sprite_threshold" },
	{ "gl_mirror_envmap", "r_mirror_envmap" },
	{ "gl_mirrors", "r_mirrors" },
	{ "gl_nocoloredspritelighting", "r_nocoloredspritelighting" },
	{ "gl_noskyboxes", "r_noskyboxes" },
	{ "gl_notexturefill", "r_notexturefill" },
	{ "gl_particles_style", "r_particles_style" },
	{ "gl_sclipfactor", "r_sclipfactor" },
	{ "gl_sclipthreshold", "r_sclipthreshold" },
	{ "gl_seamless", "r_seamless" },
	{ "gl_sprite_blend", "r_sprite_blend" },
	{ "gl_spriteclip", "r_spriteclip" },
	{ "gl_texture_filter", "r_texture_filter" },
	{ "gl_texture_filter_anisotropic", "r_texture_filter_anisotropic" },
	{ "gl_texture_hqresize", "r_texture_hqresize" },
	{ "gl_texture_hqresize_fonts", "r_texture_hqresize_fonts" },
	{ "gl_texture_hqresize_maxinputsize", "r_texture_hqresize_maxinputsize" },
	{ "gl_texture_hqresize_sprites", "r_texture_hqresize_sprites" },
	{ "gl_texture_hqresize_targets", "r_texture_hqresize_targets" },
	{ "gl_texture_hqresize_textures", "r_texture_hqresize_textures" },
	{ "gl_texture_usehires", "r_texture_usehires" },
	{ "gl_use_models", "r_use_models" },
	{ "gl_usecolorblending", "r_usecolorblending" },
	{ "gl_weaponlight", "r_weaponlight" },
};

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

// [rc4l] Whether the first `len` characters of `name` are all of `alias`, ignoring case.
bool SameName(const char *name, size_t len, const char *alias)
{
	size_t i = 0;
	for (; i < len; i++)
		if (alias[i] == 0 || Lower(name[i]) != Lower(alias[i])) return false;
	return alias[i] == 0;
}

} // namespace

const char *ComputeCVarAliasTarget(const char *name, size_t len)
{
	if (name == NULL) return NULL;
	for (const Alias &a : kAliases)
		if (SameName(name, len, a.alias)) return a.target;
	return NULL;
}

}} // namespace zx::cvaralias
