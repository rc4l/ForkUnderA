// [rc4l] A sector's floor or ceiling as GL draws it: its texture, plane, height and placement, moved here from gl/scene/gl_wall.h, which includes it.

#ifndef ZX_SECTORPLANE_H
#define ZX_SECTORPLANE_H

#include "r_defs.h"
#include "features/gamecopy/select.h"   // [rc4l] LiveSel, CopySel

struct GLSectorPlane
{
	FTextureID texture;
	secplane_core_t plane;
	fixed_t texheight;
	fixed_t xoffs,  yoffs;
	fixed_t	xscale, yscale;
	angle_t	angle;

	// [rc4l] From a sector's state as Sel picks it.
	template <class Sel> void GetFrom(sector_t * sec, int ceiling)
	{
		const auto &s = Sel::Of(sec);
		xoffs = s.GetXOffset(ceiling);
		yoffs = s.GetYOffset(ceiling);
		xscale = s.GetXScale(ceiling);
		yscale = s.GetYScale(ceiling);
		angle = s.GetAngle(ceiling);
		texture = s.GetTexture(ceiling);
		plane = s.GetSecPlane(ceiling);
		texheight = (ceiling == sector_t::ceiling)? plane.d : -plane.d;
	}
	// [rc4l] GL's: the live sector, which may be its own height transfer's scratch copy.
	void GetFromSector(sector_t * sec, int ceiling) { GetFrom<zx::gamecopy::LiveSel>(sec, ceiling); }
	// [rc4l] The backend's: the render copy.
	void GetFromCopy(sector_t * sec, int ceiling) { GetFrom<zx::gamecopy::CopySel>(sec, ceiling); }
};

#endif // ZX_SECTORPLANE_H
