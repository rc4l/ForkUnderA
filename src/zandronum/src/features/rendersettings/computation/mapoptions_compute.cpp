// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/rendersettings/computation/mapoptions_compute.h"

namespace zx { namespace rendersettings {

bool ComputeLightmodeValid(int mapLightmode)
{
	return (mapLightmode >= 0 && mapLightmode <= 4) || mapLightmode == 8;
}

namespace {

bool MapOrPlayer(int map, bool player)
{
	return map == -1 ? player : map != 0;
}

} // namespace

ResolvedRenderSettings ComputeMapRenderSettings(const MapRenderOptions &map, const PlayerRenderSettings &player)
{
	ResolvedRenderSettings r;
	// [rc4l] OVERRIDE_LIGHTMODE_IF_NECESSARY: forced defaults read the setting's default, its shader-model test always false since GL 3.
	r.lightmode = ComputeLightmodeValid(map.lightmode) ? map.lightmode : (player.forced ? player.lightmodeDefault : player.lightmode);
	r.brightfog = MapOrPlayer(map.brightfog, player.brightfog);
	r.nocoloredspritelighting = MapOrPlayer(map.nocoloredspritelighting, player.nocoloredspritelighting);
	r.notexturefill = MapOrPlayer(map.notexturefill, player.notexturefill);
	return r;
}

}} // namespace zx::rendersettings
