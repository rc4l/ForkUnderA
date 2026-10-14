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
	// [rc4l] Forced defaults read the setting's default, as GL's old override macro did with its shader-model test always false since GL 3.
	r.lightmode = ComputeLightmodeValid(map.lightmode) ? map.lightmode : (player.forced ? player.lightmodeDefault : player.lightmode);
	r.brightfog = MapOrPlayer(map.brightfog, player.brightfog);
	r.nocoloredspritelighting = MapOrPlayer(map.nocoloredspritelighting, player.nocoloredspritelighting);
	r.notexturefill = MapOrPlayer(map.notexturefill, player.notexturefill);
	return r;
}

bool ComputeGlobalFogParameter(int type, int value, int &fogDensity, int &outsideFogDensity, int &skyFog)
{
	switch (type)
	{
	case 0: fogDensity = value >> 1; return true;
	case 1: outsideFogDensity = value >> 1; return true;
	case 2: skyFog = value; return true;
	default: return false;
	}
}

}} // namespace zx::rendersettings
