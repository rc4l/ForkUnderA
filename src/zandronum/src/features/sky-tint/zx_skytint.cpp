// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_skytint.h for why nothing here writes to the world.

#include "features/sky-tint/zx_skytint.h"
#include "features/sky-tint/computation/skytint_compute.h"

#include "doomtype.h"
#include "doomstat.h"
#include "c_cvars.h"
#include "r_defs.h"
#include "r_state.h"
#include "r_sky.h"
#include "templates.h"
#include "textures/textures.h"
#include "bitmap.h"
#include "gl/renderer/gl_colormap.h"

#include <vector>

// [rc4l] cl_ because this is one player's view of the light: it changes nothing the server
// simulates, it is never sent anywhere, and two clients disagreeing about it is fine.
CUSTOM_CVAR( Bool, cl_fua_skytint, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )
{
	zx::SkyTint_Rebuild( );
}

CUSTOM_CVAR( Int, cl_fua_skytint_strength, 35, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )
{
	if ( self < 0 )			self = 0;
	else if ( self > 100 )	self = 100;
	else					zx::SkyTint_Rebuild( );
}

// How far a violently coloured sky may drag the light away from neutral.
CUSTOM_CVAR( Int, cl_fua_skytint_saturation, 60, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )
{
	if ( self < 0 )			self = 0;
	else if ( self > 100 )	self = 100;
	else					zx::SkyTint_Rebuild( );
}

// How many rooms inward the outdoor light reaches, halving each step. 0 = open sky only.
CUSTOM_CVAR( Int, cl_fua_skytint_bleed, 2, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )
{
	if ( self < 0 )			self = 0;
	else if ( self > 4 )	self = 4;
	else					zx::SkyTint_Rebuild( );
}

// 0 = mean (faithful), 1 = dominant (the colour a person would name).
CUSTOM_CVAR( Int, cl_fua_skytint_mode, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )
{
	if ( self < 0 )			self = 0;
	else if ( self > 1 )	self = 1;
	else					zx::SkyTint_Rebuild( );
}

// 0 = horizon band (what the player looks at), 1 = cosine (what actually lights a floor).
CUSTOM_CVAR( Int, cl_fua_skytint_weight, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )
{
	if ( self < 0 )			self = 0;
	else if ( self > 1 )	self = 1;
	else					zx::SkyTint_Rebuild( );
}

namespace zx
{

namespace
{

// Per sector: the light colour to substitute, or white for "leave this one alone". Sized to the
// current level and cleared with it, so an index can never outlive its sector array.
std::vector<PalEntry> g_tint;
bool g_any = false;

// Read the current sky texture into linear-friendly RGB. Returns false when there is no usable sky,
// which is the normal case indoors and must not be treated as an error.
bool ReadSky( std::vector<SkyRgb> &out, int &width )
{
	FTexture *sky = TexMan( sky1texture );
	if ( sky == NULL )
		return false;

	const int w = sky->GetWidth( ), h = sky->GetHeight( );
	if (( w <= 0 ) || ( h <= 0 ))
		return false;

	FBitmap bmp;
	if ( !bmp.Create( w, h ))
		return false;

	sky->CopyTrueColorPixels( &bmp, 0, 0 );

	// FBitmap is BGRA. Unpacked by hand rather than reusing gl_texture's averageColor, which
	// averages sRGB bytes -- the exact thing the compute unit exists to stop doing.
	const BYTE *pix = bmp.GetPixels( );
	out.clear( );
	out.reserve( (size_t)w * h );
	for ( int i = 0; i < w * h; ++i )
		out.push_back( SkyRgb( pix[i * 4 + 2], pix[i * 4 + 1], pix[i * 4 + 0] ));

	width = w;
	return true;
}

} // namespace

void SkyTint_Clear( )
{
	g_tint.clear( );
	g_any = false;
}

void SkyTint_Rebuild( )
{
	SkyTint_Clear( );

	if ( !cl_fua_skytint || ( gamestate != GS_LEVEL ) || ( sectors == NULL ) || ( numsectors <= 0 ))
		return;

	std::vector<SkyRgb> pixels;
	int width = 0;
	if ( !ReadSky( pixels, width ))
		return;

	const SkyAverage mode = ( cl_fua_skytint_mode == 1 ) ? SkyAverage::Dominant : SkyAverage::Mean;
	const SkyWeight weight = ( cl_fua_skytint_weight == 1 ) ? SkyWeight::Cosine : SkyWeight::Horizon;

	SkyRgb tint = AverageSky( pixels, width, mode, weight );
	tint = NormaliseBrightness( tint );				// the sky says WHICH colour, not how much
	tint = ClampSaturation( tint, cl_fua_skytint_saturation );

	// hop 0 = open sky, then outward through two-sided lines with a real vertical opening.
	const int maxHops = cl_fua_skytint_bleed;
	std::vector<BYTE> hop( numsectors, 255 );

	for ( int i = 0; i < numsectors; ++i )
	{
		const sector_t &s = sectors[i];
		if ( s.GetTexture( sector_t::ceiling ) != skyflatnum )
			continue;
		// A mapper or mod that coloured this sector meant it; theirs wins and we never touch it.
		if (( s.ColorMap == NULL ) || (( s.ColorMap->Color.d & 0xFFFFFF ) != 0xFFFFFF ))
			continue;

		hop[i] = 0;
	}

	for ( int pass = 1; pass <= maxHops; ++pass )
	{
		for ( int i = 0; i < numlines; ++i )
		{
			const line_t &l = lines[i];
			if (( l.frontsector == NULL ) || ( l.backsector == NULL ))
				continue;

			for ( int dir = 0; dir < 2; ++dir )
			{
				const sector_t *from = dir ? l.backsector : l.frontsector;
				sector_t *to = dir ? l.frontsector : l.backsector;
				const int fi = (int)( from - sectors ), ti = (int)( to - sectors );

				if (( hop[fi] != pass - 1 ) || ( hop[ti] != 255 ))
					continue;
				if (( to->ColorMap == NULL ) || (( to->ColorMap->Color.d & 0xFFFFFF ) != 0xFFFFFF ))
					continue;

				// Light only crosses a real gap, so a closed door stays dark.
				const fixed_t mx = ( l.v1->x + l.v2->x ) / 2, my = ( l.v1->y + l.v2->y ) / 2;
				const fixed_t openTop = MIN( from->ceilingplane.ZatPoint( mx, my ), to->ceilingplane.ZatPoint( mx, my ));
				const fixed_t openBot = MAX( from->floorplane.ZatPoint( mx, my ), to->floorplane.ZatPoint( mx, my ));
				if ( openTop <= openBot )
					continue;

				hop[ti] = (BYTE)pass;
			}
		}
	}

	g_tint.assign( numsectors, PalEntry( 255, 255, 255 ));

	for ( int i = 0; i < numsectors; ++i )
	{
		if ( hop[i] == 255 )
			continue;

		const int strength = StrengthAtHop( cl_fua_skytint_strength, hop[i], maxHops );
		if ( strength <= 0 )
			continue;

		const SkyRgb lit = BlendFromWhite( tint, strength );
		g_tint[i] = PalEntry( (BYTE)lit.r, (BYTE)lit.g, (BYTE)lit.b );
		g_any = true;
	}
}

void SkyTint_Apply( const sector_t *sec, FColormap &cm )
{
	if ( !g_any || ( sec == NULL ) || ( sectors == NULL ))
		return;

	// gl_FakeFlat hands out pointers to STACK copies as well as to the real array, so an index is
	// only meaningful for a sector that actually lives in the level's array.
	const ptrdiff_t at = sec - sectors;
	if (( at < 0 ) || ( at >= (ptrdiff_t)g_tint.size( )))
		return;

	const PalEntry &tint = g_tint[at];
	if ( tint.d == 0xFFFFFF || ( tint.r == 255 && tint.g == 255 && tint.b == 255 ))
		return;

	// Multiplied into whatever the sector already had rather than replacing it: the tint is light
	// arriving from outside, not a repaint, so a coloured sector keeps its own character.
	cm.LightColor.r = (BYTE)(( cm.LightColor.r * tint.r ) / 255 );
	cm.LightColor.g = (BYTE)(( cm.LightColor.g * tint.g ) / 255 );
	cm.LightColor.b = (BYTE)(( cm.LightColor.b * tint.b ) / 255 );
}

} // namespace zx
