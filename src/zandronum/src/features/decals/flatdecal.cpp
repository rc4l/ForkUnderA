// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/decals/flatdecal.h"
#include "farchive.h"

IMPLEMENT_CLASS (DFlatDecal)

DFlatDecal::DFlatDecal ()
: DImpactDecal (), ContactX (0), ContactY (0), ContactZ (0), AnchorSector (-1), AnchorPlane (0), AnchorOffset (0)
{
	for (int i = 0; i < 3; i++) Normal[i] = Travel[i] = 0.f;
}

DFlatDecal::DFlatDecal (fixed_t z)
: DImpactDecal (z), ContactX (0), ContactY (0), ContactZ (z), AnchorSector (-1), AnchorPlane (0), AnchorOffset (0)
{
	for (int i = 0; i < 3; i++) Normal[i] = Travel[i] = 0.f;
}

void DFlatDecal::Serialize (FArchive &arc)
{
	Super::Serialize (arc);
	arc << ContactX << ContactY << ContactZ;
	for (int i = 0; i < 3; i++) arc << Normal[i];
	for (int i = 0; i < 3; i++) arc << Travel[i];
	arc << AnchorSector << AnchorPlane << AnchorOffset;
}
