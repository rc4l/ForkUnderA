// [rc4l] A sector's floor or ceiling as GL draws it: its texture, plane, height and placement, moved here from gl/scene/gl_wall.h, which includes it.

#ifndef ZX_SECTORPLANE_H
#define ZX_SECTORPLANE_H

#include "r_defs.h"

struct GLSectorPlane
{
	FTextureID texture;
	secplane_t plane;
	fixed_t texheight;
	fixed_t xoffs,  yoffs;
	fixed_t	xscale, yscale;
	angle_t	angle;

	void GetFromSector(sector_t * sec, int ceiling)
	{
		xoffs = sec->GetXOffset(ceiling);
		yoffs = sec->GetYOffset(ceiling);
		xscale = sec->GetXScale(ceiling);
		yscale = sec->GetYScale(ceiling);
		angle = sec->GetAngle(ceiling);
		texture = sec->GetTexture(ceiling);
		plane = sec->GetSecPlane(ceiling);
		texheight = (ceiling == sector_t::ceiling)? plane.d : -plane.d;
	}
};

#endif // ZX_SECTORPLANE_H
