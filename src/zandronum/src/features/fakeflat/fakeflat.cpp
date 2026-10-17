/*
** gl_fakeflat.cpp
** Fake flat functions to render stacked sectors
**
**---------------------------------------------------------------------------
** Copyright 2001-2011 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
** 4. When not used as part of GZDoom or a GZDoom derivative, this code will be
**    covered by the terms of the GNU Lesser General Public License as published
**    by the Free Software Foundation; either version 2.1 of the License, or (at
**    your option) any later version.
** 5. Full disclosure of the entire project's source code, except for third
**    party libraries is mandatory. (NOTE: This clause is non-negotiable!)
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include "p_lnspec.h"
#include "p_local.h"
#include "a_sharedglobal.h"
#include "r_sky.h"
#include "r_defs.h"
#include "c_cvars.h"
#include "features/fakeflat/fakeflat.h"
#include "features/gamecopy/select.h"   // [rc4l] LiveSel, CopySel


//==========================================================================
//
// Check whether the player can look beyond this line
//
//==========================================================================
CVAR(Bool, gltest_slopeopt, false, 0)

template <class Sel> static bool CheckClipOf(side_t * sidedef, sector_t * frontsectorptr, sector_t * backsectorptr)
{
	const auto &frontsector_ = Sel::Of(frontsectorptr), &backsector_ = Sel::Of(backsectorptr);
	const auto &sidedef_ = Sel::Of(sidedef);
	line_t *linedef = sidedef->linedef;
	fixed_t bs_floorheight1;
	fixed_t bs_floorheight2;
	fixed_t bs_ceilingheight1;
	fixed_t bs_ceilingheight2;
	fixed_t fs_floorheight1;
	fixed_t fs_floorheight2;
	fixed_t fs_ceilingheight1;
	fixed_t fs_ceilingheight2;

	// Mirrors and horizons always block the view
	//if (linedef->special==Line_Mirror || linedef->special==Line_Horizon) return true;

	// Lines with stacked sectors must never block!

	if (backsectorptr->portals[sector_t::ceiling] != NULL || backsectorptr->portals[sector_t::floor] != NULL ||
		frontsectorptr->portals[sector_t::ceiling] != NULL || frontsectorptr->portals[sector_t::floor] != NULL)
	{
		return false;
	}

	// on large levels this distinction can save some time
	// That's a lot of avoided multiplications if there's a lot to see!

	if (frontsector_.ceilingplane.a | frontsector_.ceilingplane.b)
	{
		fs_ceilingheight1=frontsector_.ceilingplane.ZatPoint(linedef->v1);
		fs_ceilingheight2=frontsector_.ceilingplane.ZatPoint(linedef->v2);
	}
	else
	{
		fs_ceilingheight2=fs_ceilingheight1=frontsector_.ceilingplane.d;
	}

	if (frontsector_.floorplane.a | frontsector_.floorplane.b)
	{
		fs_floorheight1=frontsector_.floorplane.ZatPoint(linedef->v1);
		fs_floorheight2=frontsector_.floorplane.ZatPoint(linedef->v2);
	}
	else
	{
		fs_floorheight2=fs_floorheight1=-frontsector_.floorplane.d;
	}
	
	if (backsector_.ceilingplane.a | backsector_.ceilingplane.b)
	{
		bs_ceilingheight1=backsector_.ceilingplane.ZatPoint(linedef->v1);
		bs_ceilingheight2=backsector_.ceilingplane.ZatPoint(linedef->v2);
	}
	else
	{
		bs_ceilingheight2=bs_ceilingheight1=backsector_.ceilingplane.d;
	}

	if (backsector_.floorplane.a | backsector_.floorplane.b)
	{
		bs_floorheight1=backsector_.floorplane.ZatPoint(linedef->v1);
		bs_floorheight2=backsector_.floorplane.ZatPoint(linedef->v2);
	}
	else
	{
		bs_floorheight2=bs_floorheight1=-backsector_.floorplane.d;
	}

	// now check for closed sectors!
	if (bs_ceilingheight1<=fs_floorheight1 && bs_ceilingheight2<=fs_floorheight2) 
	{
		FTexture * tex = TexMan(sidedef_.GetTexture(side_t::top));
		if (!tex || tex->UseType==FTexture::TEX_Null) return false;
		if (backsector_.GetTexture(sector_t::ceiling)==skyflatnum && 
			frontsector_.GetTexture(sector_t::ceiling)==skyflatnum) return false;
		return true;
	}

	if (fs_ceilingheight1<=bs_floorheight1 && fs_ceilingheight2<=bs_floorheight2) 
	{
		FTexture * tex = TexMan(sidedef_.GetTexture(side_t::bottom));
		if (!tex || tex->UseType==FTexture::TEX_Null) return false;

		// properly render skies (consider door "open" if both floors are sky):
		if (backsector_.GetTexture(sector_t::ceiling)==skyflatnum && 
			frontsector_.GetTexture(sector_t::ceiling)==skyflatnum) return false;
		return true;
	}

	if (bs_ceilingheight1<=bs_floorheight1 && bs_ceilingheight2<=bs_floorheight2)
	{
		// preserve a kind of transparent door/lift special effect:
		if (bs_ceilingheight1 < fs_ceilingheight1 || bs_ceilingheight2 < fs_ceilingheight2) 
		{
			FTexture * tex = TexMan(sidedef_.GetTexture(side_t::top));
			if (!tex || tex->UseType==FTexture::TEX_Null) return false;
		}
		if (bs_floorheight1 > fs_floorheight1 || bs_floorheight2 > fs_floorheight2)
		{
			FTexture * tex = TexMan(sidedef_.GetTexture(side_t::bottom));
			if (!tex || tex->UseType==FTexture::TEX_Null) return false;
		}
		if (backsector_.GetTexture(sector_t::ceiling)==skyflatnum && 
			frontsector_.GetTexture(sector_t::ceiling)==skyflatnum) return false;
		if (backsector_.GetTexture(sector_t::floor)==skyflatnum && frontsector_.GetTexture(sector_t::floor)
			==skyflatnum) return false;
		return true;
	}

	return false;
}

//==========================================================================
//
// check for levels with exposed lower areas
//
//==========================================================================

template <class Sel> static void CheckViewAreaOf(vertex_t *v1, vertex_t *v2, sector_t *frontsector, sector_t *backsector)
{
	if (in_area==area_default && 
		(backsector->heightsec && !(Sel::Of(backsector->heightsec).MoreFlags & SECF_IGNOREHEIGHTSEC)) &&
		(!frontsector->heightsec || Sel::Of(frontsector->heightsec).MoreFlags & SECF_IGNOREHEIGHTSEC))
	{
		sector_t * s = backsector->heightsec;

		fixed_t cz1 = Sel::Of(frontsector).ceilingplane.ZatPoint(v1);
		fixed_t cz2 = Sel::Of(frontsector).ceilingplane.ZatPoint(v2);
		fixed_t fz1 = Sel::Of(s).floorplane.ZatPoint(v1);
		fixed_t fz2 = Sel::Of(s).floorplane.ZatPoint(v2);

		// allow some tolerance in case slopes are involved
		if (cz1 <= fz1 + FRACUNIT/100 && cz2<=fz2 + FRACUNIT/100) 
			in_area=area_below;
		else 
			in_area=area_normal;
	}
}


//==========================================================================
//
// This is mostly like R_FakeFlat but with a few alterations necessitated
// by hardware rendering
//
//==========================================================================
template <class Sel> static sector_t * FakeFlatOf(sector_t * sec, sector_t * dest, area_t in_area, bool back)
{
	if (!sec->heightsec || Sel::Of(sec->heightsec).MoreFlags & SECF_IGNOREHEIGHTSEC || sec->heightsec==sec) 
	{
		// check for backsectors with the ceiling lower than the floor. These will create
		// visual glitches because upper amd lower textures overlap.
		const auto &S = Sel::Of(sec);
		if (back && S.planes[sector_t::floor].TexZ > S.planes[sector_t::ceiling].TexZ)
		{
			if (!(S.floorplane.a | S.floorplane.b | S.ceilingplane.a | S.ceilingplane.b))
			{
				*dest = *sec;
				auto &D = Sel::Of(dest);
				D.ceilingplane=S.floorplane;
				D.ceilingplane.FlipVert();
				D.planes[sector_t::ceiling].TexZ = D.planes[sector_t::floor].TexZ;
				return dest;
			}
		}
		return sec;
	}

#ifdef _DEBUG
	if (sec-sectors==560)
	{
		int a = 0;
	}
#endif

	if (in_area==area_above)
	{
		if (Sel::Of(sec->heightsec).MoreFlags&SECF_FAKEFLOORONLY /*|| sec->GetTexture(sector_t::ceiling)==skyflatnum*/) in_area=area_normal;
	}

	int diffTex = (Sel::Of(sec->heightsec).MoreFlags & SECF_CLIPFAKEPLANES);
	sector_t * s = sec->heightsec;
	
#if 0
	*dest=*sec;	// This will invoke the copy operator which isn't really needed here. Memcpy is faster.
#else
	memcpy(dest, sec, sizeof(sector_t));
#endif
	auto &D = Sel::Of(dest);
	const auto &S = Sel::Of(sec);
	const auto &Src = Sel::Of(s);

	// Replace floor and ceiling height with control sector's heights.
	if (diffTex)
	{
		if (Src.floorplane.CopyPlaneIfValid (&D.floorplane, &S.ceilingplane))
		{
			Sel::SetTexture(dest, sector_t::floor, Src.GetTexture(sector_t::floor));
			Sel::SetPlaneTexZ(dest, sector_t::floor, Src.GetPlaneTexZ(sector_t::floor));
			dest->vboindex[sector_t::floor] = sec->vboindex[sector_t::vbo_fakefloor];
			dest->vboheight[sector_t::floor] = s->vboheight[sector_t::floor];
		}
		else if (Src.MoreFlags & SECF_FAKEFLOORONLY)
		{
			if (in_area==area_below)
			{
				D.ColorMap=Src.ColorMap;
				if (!(Src.MoreFlags & SECF_NOFAKELIGHT))
				{
					D.lightlevel  = Src.lightlevel;
					D.planes[sector_t::floor].Light = Src.GetPlaneLight(sector_t::floor);
					D.planes[sector_t::ceiling].Light = Src.GetPlaneLight(sector_t::ceiling);
					D.planes[sector_t::floor].Flags = Src.GetFlags(sector_t::floor);
					D.planes[sector_t::ceiling].Flags = Src.GetFlags(sector_t::ceiling);
				}
				return dest;
			}
			return sec;
		}
	}
	else
	{
		Sel::SetPlaneTexZ(dest, sector_t::floor, Src.GetPlaneTexZ(sector_t::floor));
		D.floorplane   = Src.floorplane;

		dest->vboindex[sector_t::floor] = sec->vboindex[sector_t::vbo_fakefloor];
		dest->vboheight[sector_t::floor] = s->vboheight[sector_t::floor];
	}

	if (!(Src.MoreFlags&SECF_FAKEFLOORONLY))
	{
		if (diffTex)
		{
			if (Src.ceilingplane.CopyPlaneIfValid (&D.ceilingplane, &S.floorplane))
			{
				Sel::SetTexture(dest, sector_t::ceiling, Src.GetTexture(sector_t::ceiling));
				Sel::SetPlaneTexZ(dest, sector_t::ceiling, Src.GetPlaneTexZ(sector_t::ceiling));
				dest->vboindex[sector_t::ceiling] = sec->vboindex[sector_t::vbo_fakeceiling];
				dest->vboheight[sector_t::ceiling] = s->vboheight[sector_t::ceiling];
			}
		}
		else
		{
			D.ceilingplane  = Src.ceilingplane;
			Sel::SetPlaneTexZ(dest, sector_t::ceiling, Src.GetPlaneTexZ(sector_t::ceiling));
			dest->vboindex[sector_t::ceiling] = sec->vboindex[sector_t::vbo_fakeceiling];
			dest->vboheight[sector_t::ceiling] = s->vboheight[sector_t::ceiling];
		}
	}

	if (in_area==area_below)
	{
		D.ColorMap=Src.ColorMap;
		Sel::SetPlaneTexZ(dest, sector_t::floor, S.GetPlaneTexZ(sector_t::floor));
		Sel::SetPlaneTexZ(dest, sector_t::ceiling, Src.GetPlaneTexZ(sector_t::floor));
		D.floorplane=S.floorplane;
		D.ceilingplane=Src.floorplane;
		D.ceilingplane.FlipVert();

		dest->vboindex[sector_t::floor] = sec->vboindex[sector_t::floor];
		dest->vboheight[sector_t::floor] = sec->vboheight[sector_t::floor];

		dest->vboindex[sector_t::ceiling] = sec->vboindex[sector_t::vbo_fakefloor];
		dest->vboheight[sector_t::ceiling] = s->vboheight[sector_t::floor];

		dest->portals[sector_t::ceiling] = NULL;

		if (!(Src.MoreFlags & SECF_NOFAKELIGHT))
		{
			D.lightlevel  = Src.lightlevel;
		}

		if (!back)
		{
			Sel::SetTexture(dest, sector_t::floor, diffTex ? S.GetTexture(sector_t::floor) : Src.GetTexture(sector_t::floor));
			D.planes[sector_t::floor].xform = Src.planes[sector_t::floor].xform;

			//D.ceilingplane		= Src.floorplane;
			
			if (Src.GetTexture(sector_t::ceiling) == skyflatnum) 
			{
				Sel::SetTexture(dest, sector_t::ceiling, D.GetTexture(sector_t::floor));
				//D.floorplane			= D.ceilingplane;
				//D.floorplane.FlipVert ();
				//D.floorplane.ChangeHeight (+1);
				D.planes[sector_t::ceiling].xform = D.planes[sector_t::floor].xform;

			} 
			else 
			{
				Sel::SetTexture(dest, sector_t::ceiling, diffTex ? Src.GetTexture(sector_t::floor) : Src.GetTexture(sector_t::ceiling));
				D.planes[sector_t::ceiling].xform = Src.planes[sector_t::ceiling].xform;
			}
			
			if (!(Src.MoreFlags & SECF_NOFAKELIGHT))
			{
				D.planes[sector_t::floor].Light = Src.GetPlaneLight(sector_t::floor);
				D.planes[sector_t::ceiling].Light = Src.GetPlaneLight(sector_t::ceiling);
				D.planes[sector_t::floor].Flags = Src.GetFlags(sector_t::floor);
				D.planes[sector_t::ceiling].Flags = Src.GetFlags(sector_t::ceiling);
			}
		}
	}
	else if (in_area == area_above)
	{
		D.ColorMap = Src.ColorMap;
		Sel::SetPlaneTexZ(dest, sector_t::ceiling, S.GetPlaneTexZ(sector_t::ceiling));
		Sel::SetPlaneTexZ(dest, sector_t::floor, Src.GetPlaneTexZ(sector_t::ceiling));
		D.ceilingplane = S.ceilingplane;
		D.floorplane = Src.ceilingplane;
		D.floorplane.FlipVert();

		dest->vboindex[sector_t::floor] = sec->vboindex[sector_t::vbo_fakeceiling];
		dest->vboheight[sector_t::floor] = s->vboheight[sector_t::ceiling];

		dest->vboindex[sector_t::ceiling] = sec->vboindex[sector_t::ceiling];
		dest->vboheight[sector_t::ceiling] = sec->vboheight[sector_t::ceiling];

		dest->portals[sector_t::floor] = NULL;

		if (!(Src.MoreFlags & SECF_NOFAKELIGHT))
		{
			D.lightlevel  = Src.lightlevel;
		}

		if (!back)
		{
			Sel::SetTexture(dest, sector_t::ceiling, diffTex ? S.GetTexture(sector_t::ceiling) : Src.GetTexture(sector_t::ceiling));
			Sel::SetTexture(dest, sector_t::floor, Src.GetTexture(sector_t::ceiling));
			D.planes[sector_t::ceiling].xform = D.planes[sector_t::floor].xform = Src.planes[sector_t::ceiling].xform;
			
			if (Src.GetTexture(sector_t::floor) != skyflatnum)
			{
				Sel::SetTexture(dest, sector_t::floor, Src.GetTexture(sector_t::floor));
				D.planes[sector_t::floor].xform = Src.planes[sector_t::floor].xform;
			}
			
			if (!(Src.MoreFlags & SECF_NOFAKELIGHT))
			{
				D.lightlevel  = Src.lightlevel;
				D.planes[sector_t::floor].Light = Src.GetPlaneLight(sector_t::floor);
				D.planes[sector_t::ceiling].Light = Src.GetPlaneLight(sector_t::ceiling);
				D.planes[sector_t::floor].Flags = Src.GetFlags(sector_t::floor);
				D.planes[sector_t::ceiling].Flags = Src.GetFlags(sector_t::ceiling);
			}
		}
	}
	return dest;
}


using zx::gamecopy::LiveSel;
using zx::gamecopy::CopySel;

bool gl_CheckClip(side_t * sidedef, sector_t * frontsector, sector_t * backsector)
{
	return CheckClipOf<LiveSel>(sidedef, frontsector, backsector);
}

void gl_CheckViewArea(vertex_t *v1, vertex_t *v2, sector_t *frontsector, sector_t *backsector)
{
	CheckViewAreaOf<LiveSel>(v1, v2, frontsector, backsector);
}

sector_t * gl_FakeFlat(sector_t * sec, sector_t * dest, area_t in_area, bool back)
{
	return FakeFlatOf<LiveSel>(sec, dest, in_area, back);
}

namespace zx { namespace fakeflat {

bool RcCheckClip(side_t *sidedef, sector_t *frontsector, sector_t *backsector)
{
	return CheckClipOf<CopySel>(sidedef, frontsector, backsector);
}

void RcCheckViewArea(vertex_t *v1, vertex_t *v2, sector_t *frontsector, sector_t *backsector)
{
	CheckViewAreaOf<CopySel>(v1, v2, frontsector, backsector);
}

sector_t *RcFakeFlat(sector_t *sec, sector_t *dest, area_t in_area, bool back)
{
	return FakeFlatOf<CopySel>(sec, dest, in_area, back);
}

}} // namespace zx::fakeflat

#include "features/levelmesh/computation/flatmesh_compute.h"   // [rc4l] kViewArea

namespace zx { namespace fakeflat {

// [rc4l] The view area the height transfers' planes were last drawn for.
static int g_transferArea = zx::levelmesh::kViewAreaNormal;
static_assert(area_normal == zx::levelmesh::kViewAreaNormal && area_below == zx::levelmesh::kViewAreaBelow && area_above == zx::levelmesh::kViewAreaAbove, "kViewArea spells GL's area_t");

bool ViewDependent(const sector_t *s)
{
	return s->heightsec != NULL && s->heightsec != s && !(s->heightsec->rc.MoreFlags & SECF_IGNOREHEIGHTSEC);
}

bool SetTransferArea(int area)
{
	if (area == g_transferArea) return false;
	g_transferArea = area;
	return true;
}

sector_t *TransferSector(sector_t *s, sector_t *scratch, bool back)
{
	return (s != NULL && ViewDependent(s)) ? RcFakeFlat(s, scratch, (area_t)g_transferArea, back) : s;
}

}} // namespace zx::fakeflat
