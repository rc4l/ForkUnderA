// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] MAPINFO's gl_renderer options against the player's settings, as GL resolved them at level start and on gl_resetmap.

#ifndef ZX_MAPOPTIONS_COMPUTE_H
#define ZX_MAPOPTIONS_COMPUTE_H

namespace zx { namespace rendersettings {

// [rc4l] Whether a map's MAPINFO light mode is one the renderers draw (0 to 4, or 8).
bool ComputeLightmodeValid(int mapLightmode);

// [rc4l] A map's MAPINFO options, -1 where it leaves the setting to the player.
struct MapRenderOptions
{
	int lightmode;
	int brightfog;
	int nocoloredspritelighting;
	int notexturefill;
};

// [rc4l] The player's settings for them, the light mode's default, and whether the server forces the video defaults.
struct PlayerRenderSettings
{
	int lightmode;
	int lightmodeDefault;
	bool brightfog;
	bool nocoloredspritelighting;
	bool notexturefill;
	bool forced;
};

struct ResolvedRenderSettings
{
	int lightmode;
	bool brightfog;
	bool nocoloredspritelighting;
	bool notexturefill;
};

// [rc4l] What the level draws with: the map's choice where it makes one, else the player's (the light mode's default while forced).
ResolvedRenderSettings ComputeMapRenderSettings(const MapRenderOptions &map, const PlayerRenderSettings &player);

}} // namespace zx::rendersettings

#endif // ZX_MAPOPTIONS_COMPUTE_H
