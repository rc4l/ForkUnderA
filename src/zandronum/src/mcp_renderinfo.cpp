//
// mcp_renderinfo.cpp -- console command dumping renderer / video / HUD state
// (overlay file). Read-only inspection: active backend, GL info, screen size,
// 3D viewport rect, status bar Y, plus a few render/HUD cvars. Output is one
// key=value per line (values may contain spaces), captured by the MCP bridge.
//
// Groundwork for renderer/UI inspection and splitscreen development -- the
// renderer is already per-player (Renderer->RenderView(player)), so observing
// the viewport rect + backend is what a future split-view loop needs to verify.
//
#include "doomtype.h"
#include "c_dispatch.h"
#include "c_cvars.h"
#include "v_video.h"
#include "r_main.h"
#include "r_state.h"
#include "st_stuff.h"
#include "gl/system/gl_interface.h"
#if defined( ZX_COCOA_BACKEND ) && !defined( NO_GL )
#include "gl/system/gl_system.h"
#include "sdlglvideo.h"
void I_DumpWindowGeometry();
#endif

extern int currentrenderer;

static void PrintCVar( const char *name )
{
	FBaseCVar *cvar = FindCVar( name, NULL );
	if ( cvar != NULL )
		Printf( "%s=%s\n", name, cvar->GetGenericRep( CVAR_String ).String );
}

CCMD( dumprenderer )
{
	Printf( "MCP_RENDERER\n" );
	Printf( "renderer=%s\n", currentrenderer == 1 ? "opengl" : "software" );

	if ( screen != NULL )
	{
		Printf( "screen_width=%d\n", screen->GetWidth() );
		Printf( "screen_height=%d\n", screen->GetHeight() );
	}

	// [rc4l] The DRAWABLE size in backing pixels, which on a Retina display is not the same
	// number as screen_width/height. When these two disagree the scene renders into a corner of
	// the window at a fraction of its size -- see the invariant in posix/README.md -- and there
	// was no way to see that from inside the engine, which is why it is reported here.
#if defined( ZX_COCOA_BACKEND ) && !defined( NO_GL )
	if ( screen != NULL && currentrenderer == 1 )
	{
		SDLGLFB *const fb = static_cast<SDLGLFB *>( screen );
		Printf( "client_width=%d\n", fb->GetClientWidth() );
		Printf( "client_height=%d\n", fb->GetClientHeight() );

		// What GL is ACTUALLY scissored to, as opposed to what the engine believes. A viewport
		// smaller than the drawable puts the scene in the bottom-left corner (GL's origin).
		GLint vp[4] = { 0, 0, 0, 0 };
		glGetIntegerv( GL_VIEWPORT, vp );
		Printf( "gl_viewport=%d %d %d %d\n", vp[0], vp[1], vp[2], vp[3] );

		I_DumpWindowGeometry();
		extern void ZX_DumpScaleState();
		ZX_DumpScaleState();
	}
#endif

	Printf( "view_x=%d\n", viewwindowx );
	Printf( "view_y=%d\n", viewwindowy );
	Printf( "view_width=%d\n", viewwidth );
	Printf( "view_height=%d\n", viewheight );
	Printf( "statusbar_y=%d\n", ST_Y );

	// [rc4l] The gl RenderContext lives in gl/system/gl_interface.cpp, which SERVERONLY does not
	// compile (it implies NO_GL), so referencing it unconditionally fails the server link.
#ifndef NO_GL
	if ( currentrenderer == 1 && gl.vendorstring != NULL )
	{
		Printf( "gl_vendor=%s\n", gl.vendorstring );
		Printf( "gl_shadermodel=%u\n", gl.shadermodel );
		Printf( "gl_maxtexsize=%d\n", gl.max_texturesize );
	}
#endif

	static const char *const cvars[] = {
		"vid_renderer", "fullscreen", "vid_vsync", "vid_defwidth", "vid_defheight",
		"screenblocks", "st_scale", "hud_scale", "hud_althud", "crosshair", NULL
	};
	for ( int i = 0; cvars[i] != NULL; i++ )
		PrintCVar( cvars[i] );
}

//==========================================================================
//
// [rc4l] TRIAL ONLY -- sky-derived outdoor tint prototype (dev/bridge builds).
// Averages the sky texture's horizon rows (hue only -- max component is
// normalized to 255 so brightness is untouched), then tints the light color
// of every sector whose ceiling is the sky AND whose colormap is still the
// default white. Trial-grade: bakes into sector_t::ColorMap for instant A/B
// via console; the production design is a draw-time overlay instead.
//
//==========================================================================

#include "r_sky.h"
#include "v_palette.h"
#include "bitmap.h"
#include "textures/textures.h"
#include "doomstat.h"

PalEntry averageColor( const DWORD *data, int size, fixed_t maxout_factor );

CCMD( fua_skytint )
{
	static TArray<int> tinted;   // sectors we recolored, for restore on 0 / re-apply
	static FString tintedMap;    // which level those indices belong to

	if ( argv.argc() < 2 )
	{
		Printf( "usage: fua_skytint <percent 0-60>  (0 restores)\n" );
		return;
	}
	if ( gamestate != GS_LEVEL || sectors == NULL )
		return;

	// A level (re)load rebuilds the sector array; indices recorded on another level are stale.
	if ( tintedMap.CompareNoCase( level.MapName ) != 0 )
	{
		tinted.Clear();
		tintedMap = level.MapName;
	}

	// Restore everything we touched before applying a new strength.
	for ( unsigned int i = 0; i < tinted.Size(); ++i )
	{
		if ( tinted[i] < 0 || tinted[i] >= numsectors )
			continue;
		sector_t &s = sectors[tinted[i]];
		if ( s.ColorMap == NULL )
			continue;
		s.ColorMap = GetSpecialLights( PalEntry( 255, 255, 255 ), s.ColorMap->Fade, s.ColorMap->Desaturate );
	}
	tinted.Clear();

	int pct = clamp( atoi( argv[1] ), 0, 60 );
	if ( pct == 0 )
	{
		Printf( "skytint: off\n" );
		return;
	}

	FTexture *sky = TexMan( sky1texture );
	if ( sky == NULL )
		return;
	int w = sky->GetWidth(), h = sky->GetHeight();
	if ( w <= 0 || h <= 0 )
		return;
	FBitmap bmp;
	if ( !bmp.Create( w, h ) )
		return;
	sky->CopyTrueColorPixels( &bmp, 0, 0 );

	// Horizon weighting: the lower half of the sky is the light the player perceives.
	// averageColor expects GL-order RGBA bytes but FBitmap stores BGRA, so its r/b come back
	// swapped -- swap them again on the way out.
	const DWORD *pix = (const DWORD *)bmp.GetPixels();
	PalEntry avgSw = averageColor( pix + w * ( h / 2 ), w * ( h - h / 2 ), FRACUNIT );
	PalEntry avg( 255, avgSw.b, avgSw.g, avgSw.r );

	int r = ( 255 * ( 100 - pct ) + avg.r * pct ) / 100;
	int g = ( 255 * ( 100 - pct ) + avg.g * pct ) / 100;
	int b = ( 255 * ( 100 - pct ) + avg.b * pct ) / 100;

	// hop 0 = open-sky sectors (full strength); hops 1..2 = indoor neighbours reached through a
	// two-sided line with a real vertical opening, at half strength per hop -- the bleed.
	TArray<BYTE> hop;
	hop.Resize( numsectors );
	memset( &hop[0], 255, numsectors );

	int count = 0;
	for ( int i = 0; i < numsectors; ++i )
	{
		sector_t &s = sectors[i];
		if ( s.GetTexture( sector_t::ceiling ) != skyflatnum )
			continue;
		if ( s.ColorMap == NULL || ( s.ColorMap->Color.d & 0xFFFFFF ) != 0xFFFFFF )
			continue; // mapper/mod authored a color -- theirs wins
		hop[i] = 0;
		++count;
	}

	int bled = 0;
	for ( BYTE pass = 1; pass <= 2; ++pass )
	{
		for ( int i = 0; i < numlines; ++i )
		{
			line_t &l = lines[i];
			if ( l.frontsector == NULL || l.backsector == NULL )
				continue;
			for ( int dir = 0; dir < 2; ++dir )
			{
				sector_t *from = dir ? l.backsector : l.frontsector;
				sector_t *to   = dir ? l.frontsector : l.backsector;
				int fi = (int)( from - sectors ), ti = (int)( to - sectors );
				if ( hop[fi] != pass - 1 || hop[ti] != 255 )
					continue;
				if ( to->ColorMap == NULL || ( to->ColorMap->Color.d & 0xFFFFFF ) != 0xFFFFFF )
					continue;
				// Light only passes a real gap: shared opening must be taller than zero at the
				// line's midpoint (keeps closed doors dark).
				fixed_t mx = ( l.v1->x + l.v2->x ) / 2, my = ( l.v1->y + l.v2->y ) / 2;
				fixed_t openTop = MIN( from->ceilingplane.ZatPoint( mx, my ), to->ceilingplane.ZatPoint( mx, my ));
				fixed_t openBot = MAX( from->floorplane.ZatPoint( mx, my ), to->floorplane.ZatPoint( mx, my ));
				if ( openTop <= openBot )
					continue;
				hop[ti] = pass;
				++bled;
			}
		}
	}

	for ( int i = 0; i < numsectors; ++i )
	{
		if ( hop[i] == 255 )
			continue;
		int p = pct >> hop[i]; // full / half / quarter strength by distance from the sky
		int hr = ( 255 * ( 100 - p ) + avg.r * p ) / 100;
		int hg = ( 255 * ( 100 - p ) + avg.g * p ) / 100;
		int hb = ( 255 * ( 100 - p ) + avg.b * p ) / 100;
		sector_t &s = sectors[i];
		s.ColorMap = GetSpecialLights( PalEntry( hr, hg, hb ), s.ColorMap->Fade, s.ColorMap->Desaturate );
		tinted.Push( i );
	}
	Printf( "skytint %d%%: sky horizon avg #%02x%02x%02x -> light #%02x%02x%02x on %d sky sectors + %d bled\n",
		pct, avg.r, avg.g, avg.b, r, g, b, count, bled );
}
