// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_skytint.h for why nothing here writes to the world.

#include "features/sky-tint/zx_skytint.h"
#include "features/sky-tint/computation/skytint_compute.h"

#include "doomtype.h"
#include "doomstat.h"
#include "network.h"
#include "c_cvars.h"
#include "r_defs.h"
#include "r_state.h"
#include "r_sky.h"
#include "templates.h"
#include "textures/textures.h"
#include "bitmap.h"
#include "gl/renderer/gl_colormap.h"

#include <cmath>
#include <functional>
#include <queue>
#include <utility>
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

// [rc4l] How far indoors the outdoor light reaches, in MAP UNITS -- 128 is a standard doorway's
// width, so 512 is about four of them. Measured rather than counted in sectors, because a sector
// count means something different on every map depending how finely it was cut up.
CUSTOM_CVAR( Int, cl_fua_skytint_reach, 512, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )
{
	if ( self < 0 )				self = 0;
	else if ( self > 2048 )		self = 2048;
	else						zx::SkyTint_Rebuild( );
}

// How much a narrow opening slows the light down. 0 lets a crack under a door light a room as well
// as an archway would; higher makes the size of the gap matter more.
CUSTOM_CVAR( Int, cl_fua_skytint_gap, 100, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )
{
	if ( self < 0 )				self = 0;
	else if ( self > 100 )		self = 100;
	else						zx::SkyTint_Rebuild( );
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

// Plain 2D distance in map units. Doubles rather than fixed point: this is a lighting hint that
// never touches the simulation, so precision is free and overflow is not a worry.
double Distance2( double ax, double ay, double bx, double by )
{
	const double dx = bx - ax, dy = by - ay;
	return std::sqrt(( dx * dx ) + ( dy * dy ));
}

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

	// [rc4l] NEVER on a server. P_SetupLevel runs there too, and this reads the sky TEXTURE -- which
	// a dedicated server has no business touching and does not keep in a usable state. Doing it
	// anyway killed every hosted server the moment the cvar was on, and because the cvar is
	// ARCHIVE|GLOBALCONFIG, one player enabling it in their own game poisoned the shared ini and
	// every server spawned afterwards died at map load. A client-side look setting must be inert
	// wherever there is nothing to look at.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		return;

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

	// [rc4l] Dijkstra outward from open sky, measured in MAP UNITS. Counting sectors made the reach
	// depend on how finely the mapper cut their geometry -- two hops crosses a room in a blocky map
	// and dies inside one doorway's trim in a detailed one. Distance does not care how many pieces a
	// room was built from, so the setting means one thing on every map.
	const double reach = (double)cl_fua_skytint_reach;
	const double kInfinity = 1e30;

	std::vector<double> dist( numsectors, kInfinity );
	std::priority_queue<std::pair<double, int>, std::vector<std::pair<double, int> >,
		std::greater<std::pair<double, int> > > queue;

	for ( int i = 0; i < numsectors; ++i )
	{
		const sector_t &s = sectors[i];
		if ( s.GetTexture( sector_t::ceiling ) != skyflatnum )
			continue;
		// A mapper or mod that coloured this sector meant it; theirs wins and we never touch it.
		if (( s.ColorMap == NULL ) || (( s.ColorMap->Color.d & 0xFFFFFF ) != 0xFFFFFF ))
			continue;

		dist[i] = 0.0;
		queue.push( std::make_pair( 0.0, i ));
	}

	// Every two-sided line, indexed by the sector it leads out of, so the walk does not rescan the
	// whole level per step. Built once; a big map has far more lines than a sector has neighbours.
	std::vector<std::vector<int> > linesOf( numsectors );
	for ( int i = 0; i < numlines; ++i )
	{
		const line_t &l = lines[i];
		if (( l.frontsector == NULL ) || ( l.backsector == NULL ))
			continue;

		linesOf[(int)( l.frontsector - sectors )].push_back( i );
		linesOf[(int)( l.backsector - sectors )].push_back( i );
	}

	while ( !queue.empty( ))
	{
		const double at = queue.top( ).first;
		const int here = queue.top( ).second;
		queue.pop( );

		if ( at > dist[here] )
			continue;					// a shorter way here was already found
		if ( at >= reach )
			continue;					// nothing past the limit can be lit

		for ( size_t n = 0; n < linesOf[here].size( ); ++n )
		{
			const line_t &l = lines[linesOf[here][n]];
			const sector_t *from = &sectors[here];
			sector_t *to = ( l.frontsector == from ) ? l.backsector : l.frontsector;
			const int ti = (int)( to - sectors );

			if ( to == from )
				continue;
			if (( to->ColorMap == NULL ) || (( to->ColorMap->Color.d & 0xFFFFFF ) != 0xFFFFFF ))
				continue;

			// The gap light has to get through. A closed door passes nothing; a slit passes a
			// little, at the cost of counting as a longer journey.
			const fixed_t mx = ( l.v1->x + l.v2->x ) / 2, my = ( l.v1->y + l.v2->y ) / 2;
			const fixed_t openTop = MIN( from->ceilingplane.ZatPoint( mx, my ), to->ceilingplane.ZatPoint( mx, my ));
			const fixed_t openBot = MAX( from->floorplane.ZatPoint( mx, my ), to->floorplane.ZatPoint( mx, my ));
			if ( openTop <= openBot )
				continue;

			const double opening = FIXED2FLOAT( openTop - openBot );
			const double full = FIXED2FLOAT( MAX( from->ceilingplane.ZatPoint( mx, my ) - from->floorplane.ZatPoint( mx, my ),
				to->ceilingplane.ZatPoint( mx, my ) - to->floorplane.ZatPoint( mx, my )));

			// Centre to the doorway to the next centre: an approximation of the path light takes,
			// and one that costs nothing since every sector already carries its own centre point.
			const double toDoor = Distance2( FIXED2FLOAT( from->soundorg[0] ), FIXED2FLOAT( from->soundorg[1] ),
				FIXED2FLOAT( mx ), FIXED2FLOAT( my ));
			const double fromDoor = Distance2( FIXED2FLOAT( mx ), FIXED2FLOAT( my ),
				FIXED2FLOAT( to->soundorg[0] ), FIXED2FLOAT( to->soundorg[1] ));

			// cl_fua_skytint_gap decides how much the size of the gap matters: at 0 every opening
			// counts as wide open, at 100 a half-height gap costs twice the distance.
			const double raw = OpeningFactor( opening, full );
			const double blend = cl_fua_skytint_gap / 100.0;
			const double factor = ( raw * blend ) + ( 1.0 - blend );

			const double step = StepCost( toDoor + fromDoor, factor );
			if ( step < 0.0 )
				continue;				// impassable

			const double next = at + step;
			if (( next < dist[ti] ) && ( next < reach ))
			{
				dist[ti] = next;
				queue.push( std::make_pair( next, ti ));
			}
		}
	}

	g_tint.assign( numsectors, PalEntry( 255, 255, 255 ));

	for ( int i = 0; i < numsectors; ++i )
	{
		// Distance 0 is the open sky itself and is always lit: reach controls how far the light
		// travels INDOORS, so setting it to nothing should mean "outdoors only", not "off".
		if (( dist[i] > 0.0 ) && ( dist[i] >= reach ))
			continue;
		if ( dist[i] >= kInfinity )
			continue;					// never reached at all

		const int strength = StrengthAtDistance( cl_fua_skytint_strength, dist[i], reach );
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
