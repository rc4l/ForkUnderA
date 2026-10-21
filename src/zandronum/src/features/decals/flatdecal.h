// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A mark on a floor or a ceiling as a decal of the level: saved with it, counted against cl_maxdecals with the wall decals, animated by DECALDEF, and drawn by features/levelmesh's projection from where it landed.

#ifndef ZX_FLATDECAL_H
#define ZX_FLATDECAL_H

#include "g_shared/a_sharedglobal.h"   // [rc4l] DImpactDecal

class DFlatDecal : public DImpactDecal
{
	DECLARE_CLASS (DFlatDecal, DImpactDecal)
	friend class zx::decals::Maker;
public:
	void Serialize (FArchive &arc);

	fixed_t ContactX, ContactY, ContactZ;   // [rc4l] where it landed, which a DECALDEF slider never moves
	float Normal[3];        // [rc4l] the plane's, up for a floor and down for a ceiling, tilted on a slope
	float Travel[3];        // [rc4l] the way the hit was going, for an angled mark; zero for none
	int AnchorSector;       // [rc4l] the sector whose plane it rides, a 3D floor's model sector included
	int AnchorPlane;        // [rc4l] that sector's floor (0) or ceiling (1)
	fixed_t AnchorOffset;   // [rc4l] its height above that plane where it landed

private:
	DFlatDecal ();
	DFlatDecal (fixed_t z);
};

#endif // ZX_FLATDECAL_H
