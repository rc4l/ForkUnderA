// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_skytint.h for why nothing here writes to the world.

// [rc4l] Before everything else, like features/hitboxviz: the system GL headers have to land before
// anything that pulls in the renderer, and the feature header cannot pull them in for us.
#include "gl/system/gl_system.h"

#include "features/sky-tint/zx_skytint.h"
#include "features/sky-tint/computation/skytint_compute.h"

#include "doomtype.h"
#include "doomstat.h"
#include "d_player.h"					// players[], for the "here" diagnostic
#include "p_local.h"					// R_PointInSubsector
#include "network.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "r_defs.h"
#include "r_state.h"
#include "r_sky.h"
#include "templates.h"
#include "textures/textures.h"
#include "bitmap.h"
#include "g_level.h"
#include "g_shared/a_sharedglobal.h"	// ASkyViewpoint
#include "r_utility.h"					// FCanvasTextureInfo
#include "gl/renderer/gl_colormap.h"
#include "gl/textures/gl_material.h"	// FMaterial, to bind the rendered canvas for readback

// [rc4l] Declared with CVAR() in gl/scene/gl_sky.cpp and never exported. Matched here so a sector
// with a skybox is classified the same way the renderer classifies it, including when someone turns
// skyboxes off and the renderer falls back to the texture sky.
EXTERN_CVAR( Bool, gl_noskyboxes )

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
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

// [rc4l] A CEILING on the push, applied after the distance falloff, so a violent sky is flattened at
// the bright end while the fade into shelter is left alone. It used to cap the sky's own saturation
// before the tint was derived, which stopped meaning anything once the push became equal-per-hue:
// the solve just blended further and landed in the same place. Name kept so configs survive.
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

// [rc4l] Four dials were removed here after measuring them, not after arguing about them.
//
//   Sky area (horizon vs cosine)  landed 0.19 and 0.21 apart out of 255 on two of three skies
//   Follow sky brightness         cut the tint 90% and 86% on the two maps it existed to separate
//   Sky colour from (dominant)    returned white on a mostly-black sky and switched the feature off
//   Doorway matters               a sub-dial of Indoor reach that was never verified on its own
//
// Every one of them is gone at its DEFAULT value, so a player who never touched them sees no change:
// the mean, the horizon band, no brightness scaling, and openings counting in full.

// [rc4l] How much a sector's OWN light level scales the tint. This is the dial that tells a dark
// room from a bright yard, which no sky-side control can: the sky is the same for both. A dim
// sector showing little of the light's colour is also just what light does.
CUSTOM_CVAR( Int, cl_fua_skytint_lit, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )
{
	if ( self < 0 )				self = 0;
	else if ( self > 100 )		self = 100;
	else						zx::SkyTint_Rebuild( );
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
std::vector<PalEntry> g_tint;			// per SUBSECTOR: what the renderer actually draws
std::vector<PalEntry> g_brightestOf;	// per SECTOR: its lightest leaf, for the few sector-only callers

// [rc4l] TEMPORARY, for fua_skytintinfo: the propagation distance the last rebuild settled on for
// each leaf. Kept because "this room is not lit" has two answers -- no open path to sky at all, or a
// path longer than the reach allows -- and raising the reach and seeing nothing change is evidence
// for the first but not proof of it. A leaf still at infinity was never reached by any route.
std::vector<double> g_dist;

// Shared by the rebuild's Dijkstra and the diagnostic that reads its output, so "unreachable" means
// the same number in both places.
const double kSkyTintInfinity = 1e30;
bool g_any = false;

// Which sky the table above was built from, so SkyTint_SkyChanged can tell a real sky swap from the
// view-size changes that share its call sites. Invalid until the first successful build.
// Both, not just sky1: a swapped or doubled sky is drawn from sky2texture, so a change there is a
// change to what the player sees even when sky1texture has not moved.
FTextureID g_builtForSky = FNullTextureID( );
FTextureID g_builtForSky2 = FNullTextureID( );

// How many sky-seeing leaves render a 3D skybox rather than a texture. Nothing lights them yet;
// this exists so the state is visible instead of silently absent.
int g_skyboxLeaves = 0;

// [rc4l] What the last rebuild actually derived, for the diagnostic. Reported because "the table is
// built" and "the table is doing what you think" are different claims, and comparing two maps by
// screenshotting them measures the viewpoint as much as the tint.
std::vector<SkyRgb> g_lastTints;
std::vector<SkyRgb> g_lastRaw;
std::vector<int> g_lastStrength;
SkyRgb g_lastAlbedo( 128, 128, 128 );

// Somewhere outdoors on this level, for `warp`. See the rebuild for why this exists.
double g_lookAtX = 0.0, g_lookAtY = 0.0;
bool g_lookAtValid = false;

// [rc4l] CIELAB units of visible change per point of the Strength slider. 100 therefore aims at a
// dE of 12, which is "obviously a different colour" without being a costume change; 35, the default,
// aims at about 4, around the point where a difference stops being subtle.
//
// This constant is where the dial's meaning now lives. It is a taste value and the only one in the
// file, which is the trade for the slider meaning the same thing on every map.
const double kDeltaPerPct = 0.12;

// Read one texture into linear-friendly RGB, and its alpha alongside for layering. Returns false
// when there is no usable image, which is the normal case indoors and is not an error.
bool ReadTexture( FTextureID id, std::vector<SkyRgb> &out, std::vector<int> &alpha, int &width )
{
	FTexture *sky = TexMan( id );
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
	alpha.clear( );
	out.reserve( (size_t)w * h );
	alpha.reserve( (size_t)w * h );
	for ( int i = 0; i < w * h; ++i )
	{
		out.push_back( SkyRgb( pix[i * 4 + 2], pix[i * 4 + 1], pix[i * 4 + 0] ));
		alpha.push_back( pix[i * 4 + 3] );
	}

	width = w;
	return true;
}

// [rc4l] Which sky textures a given sector actually shows, mirroring GLWall::SkyPlane in
// gl/scene/gl_sky.cpp (the `sector->GetTexture(plane)==skyflatnum` branch, its PL_SKYFLAT lookup and
// its `normalsky` label). Three things a map can do that reading the global sky1texture misses:
//
//   Init_TransferSky (p_spec.cpp)  a sector carries its own sky, taken from a linedef's sidedef, so
//                                  two sectors in one map can show different skies.
//   LEVEL_SWAPSKIES                the engine draws sky2texture. Reading sky1texture here is not an
//                                  approximation, it is a different image entirely.
//   LEVEL_DOUBLESKY                sky1texture is drawn IN FRONT of sky2texture, and what the player
//                                  sees is the composite of the two.
//
// PROVENANCE: NO UPSTREAM COMMIT -- ours. This mirrors upstream logic rather than calling it,
//   deliberately: extracting a shared helper would mean refactoring a vendored renderer file that we
//   re-sync, and this feature is not worth a permanent conflict there. The cost is that it can drift.
//   ON PORT: if gl_sky.cpp's sky selection changes upstream, re-read it and update this to match.
//   The two are expected to agree; nothing enforces it.
// One distinct sky in the level, reduced to what the light actually needs: its colour, and how hard
// it pushes. Levels almost always have exactly one of these; maps using Init_TransferSky have more.
struct SkySource
{
	SkyRgb tint;
	int strengthPct;

	SkySource( ) : tint( 255, 255, 255 ), strengthPct( 0 ) { }
};

// [rc4l] What the tint is going to be multiplied INTO, averaged over the surfaces it will touch.
//
// This is the piece the feature never had. A tint's visible effect is not a property of the tint: it
// is the perceptual distance between a surface and that surface tinted, and a surface the tint
// already agrees with barely moves. Speed of Doom is the case that proves it -- MAP20's tint is more
// saturated than MAP01's (99% against 66%) and yet it is MAP01 that overwhelms, because MAP01's tint
// is green against brown brick while MAP20's is orange against brown brick.
//
// Sampled from the FLOOR flats of the sectors that will actually be lit. Floors because they are the
// large surfaces a player sees lit from above, and because a flat is 64x64 and cheap. Distinct
// textures are averaged once each, so a level made of six flats costs six reads however many sectors
// it has.
SkyRgb SceneAlbedo( const std::vector<int> &litSectors )
{
	std::map<int, SkyRgb> seen;
	double lr = 0.0, lg = 0.0, lb = 0.0;
	int counted = 0;

	for ( size_t i = 0; i < litSectors.size( ); ++i )
	{
		const sector_t *s = &sectors[litSectors[i]];
		const FTextureID flat = s->GetTexture( sector_t::floor );
		const int key = flat.GetIndex( );

		std::map<int, SkyRgb>::const_iterator got = seen.find( key );
		if ( got == seen.end( ))
		{
			std::vector<SkyRgb> pixels;
			std::vector<int> alpha;
			int width = 0;
			if ( !ReadTexture( flat, pixels, alpha, width ) || pixels.empty( ))
			{
				seen[key] = SkyRgb( -1, -1, -1 );		// unreadable; remembered so we skip it fast
				continue;
			}

			// Plain linear mean over the whole flat: no row weighting, because a floor texture has no
			// horizon and its rows mean nothing in particular.
			double pr = 0.0, pg = 0.0, pb = 0.0;
			for ( size_t p = 0; p < pixels.size( ); ++p )
			{
				pr += LinearFromSrgb( pixels[p].r );
				pg += LinearFromSrgb( pixels[p].g );
				pb += LinearFromSrgb( pixels[p].b );
			}

			const double n = (double)pixels.size( );
			got = seen.insert( std::make_pair( key, SkyRgb( SrgbFromLinear( pr / n ),
				SrgbFromLinear( pg / n ), SrgbFromLinear( pb / n )))).first;
		}

		if ( got->second.r < 0 )
			continue;

		lr += LinearFromSrgb( got->second.r );
		lg += LinearFromSrgb( got->second.g );
		lb += LinearFromSrgb( got->second.b );
		++counted;
	}

	if ( counted <= 0 )
		return SkyRgb( 128, 128, 128 );		// nothing readable: assume neutral mid-grey

	return SkyRgb( SrgbFromLinear( lr / counted ), SrgbFromLinear( lg / counted ),
		SrgbFromLinear( lb / counted ));
}

struct SectorSky
{
	FTextureID front;		// the layer drawn nearest, or the only layer
	FTextureID back;		// valid only for a double sky, drawn behind `front`
	bool doubled;
	ASkyViewpoint *box;		// non-NULL when this sector renders a 3D skybox instead of a texture

	SectorSky( ) : front( FNullTextureID( )), back( FNullTextureID( )), doubled( false ), box( NULL ) { }
};

SectorSky SkyForSector( const sector_t *sec )
{
	SectorSky out;
	if ( sec == NULL )
		return out;

	// A skybox wins over any texture: the sector renders a whole scene from a viewpoint actor, and
	// there is no image here to average. Reported so the caller can decide what to do about it.
	if ( !gl_noskyboxes )
	{
		// const_cast because GetSkyBox is non-const upstream; it only reads.
		sector_t *mutableSec = const_cast<sector_t *>( sec );
		ASkyViewpoint *boxx = mutableSec->GetSkyBox( sector_t::ceiling );
		if ( boxx != NULL )
		{
			out.box = boxx;
			return out;
		}
	}

	const int sky1 = sec->sky;
	if (( sky1 & PL_SKYFLAT ) && ( sky1 & ( PL_SKYFLAT - 1 )))
	{
		const line_t *l = &lines[( sky1 & ( PL_SKYFLAT - 1 )) - 1];
		const side_t *s = l->sidedef[0];
		if ( s != NULL )
		{
			const int pos = (( level.flags & LEVEL_SWAPSKIES ) && s->GetTexture( side_t::bottom ).isValid( ))
				? side_t::bottom : side_t::top;

			const FTextureID tex = s->GetTexture( pos );
			FTexture *t = TexMan( tex );
			if (( t != NULL ) && ( t->UseType != FTexture::TEX_Null ))
			{
				out.front = tex;
				return out;
			}
			// Falls through to the level sky, exactly as gl_sky.cpp's `goto normalsky` does.
		}
	}

	if ( level.flags & LEVEL_DOUBLESKY )
	{
		out.doubled = true;
		out.front = sky1texture;
		out.back = ( sky2texture != sky1texture ) ? sky2texture : sky1texture;
		return out;
	}

	const bool useSky2 = (( level.flags & LEVEL_SWAPSKIES ) || ( sky1 == PL_SKYFLAT ))
		&& ( sky2texture != sky1texture );
	out.front = useSky2 ? sky2texture : sky1texture;
	return out;
}

// [rc4l] Sampling a 3D skybox.
//
// A skybox is not an image, it is a camera: the engine renders the level from a SkyViewpoint actor
// and shows that where the sky would be. So the only honest way to know its colour is to render it
// and look, which is what a real engine does for reflection probes too.
//
// The pieces are all the engine's own. FCanvasTextureInfo::Add is how a map wires a camera to a
// texture, and FCanvasTextureInfo::UpdateAll (called once per frame from gl_scene) renders every
// registered one. Going through that rather than calling RenderTextureView directly matters:
// UpdateAll saves and restores the fixedcolormap globals that camera rendering clobbers, and it
// respects bNeedsUpdate. Nothing ever draws our texture on a surface, so nothing re-arms
// bNeedsUpdate, which means it renders once and then stops on its own rather than costing a scene
// render every frame forever.
//
// 32x32 is deliberate: we want one average colour, and a smaller render is a cheaper one. Power of
// two so there is no padding to skip past on readback.
const int kBoxSampleSize = 32;

FCanvasTexture *g_boxCanvas = NULL;
FTextureID g_boxCanvasId = FNullTextureID( );
ASkyViewpoint *g_boxPendingFor = NULL;		// registered and waiting for a frame to render it

// Viewpoints still needing a sample, and the answers for the ones already done. Keyed by actor
// pointer, which is safe only because both are cleared with the level (SkyTint_Clear).
std::vector<ASkyViewpoint *> g_boxQueue;
std::map<ASkyViewpoint *, SkyRgb> g_boxDone;

// Read the canvas back off the GPU. In GL a canvas texture lives in an FBO and its CPU-side pixels
// are never filled, so GetPixels() would hand back an empty buffer -- checked, not assumed.
bool ReadCanvas( std::vector<SkyRgb> &out, int &width )
{
	if ( g_boxCanvas == NULL )
		return false;

	FMaterial *mat = FMaterial::ValidateTexture( g_boxCanvas, false );
	if ( mat == NULL )
		return false;

	mat->Bind( 0, 0 );

	const int w = kBoxSampleSize, h = kBoxSampleSize;
	std::vector<unsigned char> buf( (size_t)w * h * 4, 0 );
	glGetTexImage( GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, &buf[0] );

	// An all-black read means the render has not landed yet. Treated as "not ready" rather than as a
	// black sky, because tinting the world black off a failed read would be a spectacular way to be
	// wrong.
	bool anyLight = false;
	out.clear( );
	out.reserve( (size_t)w * h );
	for ( int i = 0; i < w * h; ++i )
	{
		const int r = buf[i * 4 + 0], g = buf[i * 4 + 1], b = buf[i * 4 + 2];
		if (( r | g | b ) != 0 )
			anyLight = true;
		out.push_back( SkyRgb( r, g, b ));
	}

	width = w;
	return anyLight;
}

// The colour of one sector's sky, composited if it is a double sky. False when there is nothing
// usable to read.
bool ReadSkyForSector( const sector_t *sec, std::vector<SkyRgb> &out, int &width )
{
	const SectorSky sky = SkyForSector( sec );

	std::vector<int> alpha;
	if ( !ReadTexture( sky.front, out, alpha, width ))
		return false;

	if ( !sky.doubled || ( sky.back == sky.front ))
		return true;

	std::vector<SkyRgb> back;
	std::vector<int> backAlpha;
	int backWidth = 0;
	if ( !ReadTexture( sky.back, back, backAlpha, backWidth ))
		return true;			// no usable back layer; the front one stands on its own

	out = CompositeSkyLayers( out, alpha, width, back, backWidth );
	return !out.empty( );
}

// The per-leaf tables only. Split out from SkyTint_Clear so a rebuild can reset them WITHOUT
// throwing away sampled skyboxes, which is what the frame hook needs.
void ClearTables( )
{
	g_tint.clear( );
	g_brightestOf.clear( );
	g_any = false;
	g_builtForSky = FNullTextureID( );
	g_builtForSky2 = FNullTextureID( );
	g_skyboxLeaves = 0;
	g_lastTints.clear( );
	g_lastRaw.clear( );
	g_lastStrength.clear( );
	g_lastAlbedo = SkyRgb( 128, 128, 128 );
	g_dist.clear( );
}

// Sampled skyboxes, keyed by actor pointer and therefore only valid within one level.
void ForgetSkyboxes( )
{
	g_boxQueue.clear( );
	g_boxDone.clear( );
	g_boxPendingFor = NULL;
}

} // namespace

// [rc4l] TEMPORARY diagnostic: what does the rebuild actually see? Added chasing sky tint dying
// after a wad reload, where the same map tints fine on a fresh launch. Delete once answered.
CCMD( fua_skytintinfo )
{
	FTexture *sky = TexMan( sky1texture );
	Printf( "skytint: state=%d gamestate=%d cvar=%d sectors=%d subsectors=%d\n",
		(int)NETWORK_GetState( ), (int)gamestate, (int)(bool)cl_fua_skytint, numsectors, numsubsectors );
	// [rc4l] The NAME as well as the index: an index cannot be fed back to `changesky`, so comparing
	// one map's sky against another's meant looking the texture up by hand every time.
	Printf( "skytint: sky1texture=%d name=%s tex=%p w=%d h=%d skyflatnum=%d\n",
		sky1texture.GetIndex( ), ( sky && sky->Name[0] ) ? sky->Name : "?", (void *)sky,
		sky ? sky->GetWidth( ) : -1, sky ? sky->GetHeight( ) : -1, skyflatnum.GetIndex( ));
	Printf( "skytint: table=%u lit=%u any=%d skyboxleaves=%d doublesky=%d swapskies=%d sky2=%d\n",
		(unsigned)zx::SkyTintTableSize( ), (unsigned)zx::SkyTintLitLeaves( ),
		(int)zx::SkyTint_Active( ), zx::SkyTintSkyboxLeaves( ),
		(int)!!( level.flags & LEVEL_DOUBLESKY ), (int)!!( level.flags & LEVEL_SWAPSKIES ),
		sky2texture.GetIndex( ));

	std::string boxes;
	zx::SkyTintSkyboxSamples( boxes );
	Printf( "skytint: skyboxes=%s\n", boxes.c_str( ));

	// The colour the algorithm actually derived, raw and after shaping. This is what to compare
	// between two maps: a screenshot measures the viewpoint as much as the tint.
	std::string derived;
	zx::SkyTintDerived( derived );
	Printf( "skytint: derived=%s\n", derived.c_str( ));

	// A place to stand where the sky can actually be seen. Printed as a ready-made command because
	// judging an outdoor tint from wherever the player happens to spawn is how several measurements
	// in this feature's history went wrong.
	double lx = 0.0, ly = 0.0;
	if ( zx::SkyTintOutdoorSpot( lx, ly ))
		Printf( "skytint: outdoors: warp %d %d\n", (int)lx, (int)ly );
	else
		Printf( "skytint: outdoors: none found\n" );

	// [rc4l] What the table holds for the leaf the player is STANDING IN, and what the sector-level
	// answer for that same spot would be. Without this, "the tint is missing here" has two completely
	// different causes that look identical from inside the game: the table having nothing for this
	// leaf (propagation), or having something the renderer never draws (a draw path that misses it).
	// Guessing between those two burned two builds on the 3D floor case.
	std::string here;
	zx::SkyTintHere( here );
	Printf( "skytint: here: %s\n", here.c_str( ));
}

void SkyTint_Clear( )
{
	ClearTables( );

	// Skybox results are keyed by actor pointer, which only stays meaningful within one level. The
	// canvas texture itself is kept: it belongs to TexMan now and is reused for the next level.
	ForgetSkyboxes( );
}

void SkyTint_SkyChanged( )
{
	if (( g_builtForSky == sky1texture ) && ( g_builtForSky2 == sky2texture ))
		return;

	SkyTint_Rebuild( );
}

void SkyTint_Rebuild( )
{
	// [rc4l] NOT SkyTint_Clear: that also forgets sampled skyboxes, and this function is what the
	// frame hook calls right after caching one. Clearing here would drop the answer, re-queue the
	// same viewpoint, and spin forever rendering it. The skybox cache is dropped on a level change
	// instead, detected below.
	//
	// The subsector array is the level-change signal: a new level allocates a new one, and the actor
	// pointers the cache is keyed by belong to the level that is going away.
	static subsector_t *lastLevel = NULL;
	if ( subsectors != lastLevel )
	{
		ForgetSkyboxes( );
		lastLevel = subsectors;
	}

	ClearTables( );

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

	// [rc4l] Dijkstra outward from open sky over SUBSECTORS, measured in map units.
	//
	// Two problems, one answer. Counting sector hops made the reach depend on how finely the mapper
	// cut their geometry; measuring distance fixes that. And lighting a whole sector at one value
	// made the effect look like a flood rather than light -- a room beside a lit yard came up evenly
	// bright to its far corner. BSP leaves are small and the renderer already draws them one at a
	// time, so the same room now fades across itself.
	//
	// Adjacency is free: every seg carries its PartnerSeg, so leaves inside one sector are joined
	// with no opening test at all, which is what produces the gradient indoors.
	const double reach = (double)cl_fua_skytint_reach;
	const double &kInfinity = kSkyTintInfinity;

	if (( subsectors == NULL ) || ( numsubsectors <= 0 ))
		return;

	// Leaf centres, averaged from the leaf's own corners. Cheap, and needed because a subsector has
	// no centre point of its own the way a sector does.
	std::vector<double> cx( numsubsectors, 0.0 ), cy( numsubsectors, 0.0 );
	for ( int i = 0; i < numsubsectors; ++i )
	{
		const subsector_t &sub = subsectors[i];
		if ( sub.numlines == 0 )
			continue;

		double sx = 0.0, sy = 0.0;
		for ( DWORD k = 0; k < sub.numlines; ++k )
		{
			sx += FIXED2FLOAT( sub.firstline[k].v1->x );
			sy += FIXED2FLOAT( sub.firstline[k].v1->y );
		}
		cx[i] = sx / (double)sub.numlines;
		cy[i] = sy / (double)sub.numlines;
	}

	std::vector<double> dist( numsubsectors, kInfinity );
	std::priority_queue<std::pair<double, int>, std::vector<std::pair<double, int> >,
		std::greater<std::pair<double, int> > > queue;

	// [rc4l] One entry per DISTINCT sky in the level, and which one every leaf is lit by. A map can
	// show different skies in different sectors (Init_TransferSky), so there is no single answer to
	// "what colour is the sky here" -- the seeds carry their own colour outward and the Dijkstra
	// front decides. Nearest source wins; where two fronts meet there is a seam rather than a blend,
	// which is deliberate for now.
	std::vector<SkySource> sources;
	std::vector<int> litBy( numsubsectors, -1 );
	std::map<int, int> sourceOfTexture;					// texture index -> index into `sources`
	std::map<ASkyViewpoint *, int> boxSourceIdx;		// skybox viewpoint -> index into `sources`

	// [rc4l] What the tint will land on, gathered before any source is built because the strength of
	// each source is chosen against it. Sky-ceilinged sectors only: those are the ones lit hardest and
	// the ones a player is looking at when they judge whether the effect is too strong.
	std::vector<int> litSectors;
	for ( int i = 0; i < numsectors; ++i )
	{
		if ( sectors[i].GetTexture( sector_t::ceiling ) == skyflatnum )
			litSectors.push_back( i );
	}

	const SkyRgb albedo = SceneAlbedo( litSectors );
	g_lastAlbedo = albedo;

	// [rc4l] Remember the BIGGEST sky-lit subsector's centre, so the diagnostic can hand out a place
	// to stand. Every visual comparison made while building this feature was taken at the player
	// start, which on a lot of maps is indoors: one such pair measured a dark room at R12 G9 B7 and
	// was nearly used as evidence about an outdoor tint. Largest leaf rather than first because a
	// big one is more likely to be open ground than a doorstep.
	g_lookAtValid = false;
	{
		double bestArea = 0.0;
		for ( int i = 0; i < numsubsectors; ++i )
		{
			const subsector_t &sub = subsectors[i];
			const sector_t *s = sub.sector;
			if (( s == NULL ) || ( s->GetTexture( sector_t::ceiling ) != skyflatnum ))
				continue;
			if ( sub.numlines < 3 )
				continue;

			// Shoelace over the leaf's own edge vertices. A BSP leaf is convex, so this is its area.
			double area = 0.0, sx = 0.0, sy = 0.0;
			for ( DWORD k = 0; k < sub.numlines; ++k )
			{
				const double x1 = FIXED2FLOAT( sub.firstline[k].v1->x );
				const double y1 = FIXED2FLOAT( sub.firstline[k].v1->y );
				const double x2 = FIXED2FLOAT( sub.firstline[k].v2->x );
				const double y2 = FIXED2FLOAT( sub.firstline[k].v2->y );
				area += ( x1 * y2 ) - ( x2 * y1 );
				sx += x1;
				sy += y1;
			}

			area = ( area < 0.0 ) ? -area : area;
			if ( area > bestArea )
			{
				bestArea = area;
				g_lookAtX = sx / (double)sub.numlines;
				g_lookAtY = sy / (double)sub.numlines;
				g_lookAtValid = true;
			}
		}
	}

	for ( int i = 0; i < numsubsectors; ++i )
	{
		const sector_t *s = subsectors[i].sector;
		if (( s == NULL ) || ( s->GetTexture( sector_t::ceiling ) != skyflatnum ))
			continue;
		// [rc4l] A coloured sector still SEEDS; how much sky light it keeps is decided at draw time by
		// SkyShareForSectorColour. This used to skip it outright, which meant a sector the mapper had
		// touched at all got nothing AND lit none of its neighbours. Eon Collection aeon13 lays one
		// faint [254,194,194] wash over the whole level, and that switched the feature off across 158
		// of 180 sky-seeing spots.
		if ( s->ColorMap == NULL )
			continue;

		// Keyed on the front texture: two sectors showing the same sky share one average rather than
		// paying for it twice. A level with one sky therefore costs exactly what it used to.
		const SectorSky sky = SkyForSector( s );

		if ( sky.box != NULL )
		{
			++g_skyboxLeaves;

			// Already sampled: it seeds like any other sky. Not yet: queue it and leave this leaf
			// dark for now. SkyTint_FrameHook renders it and rebuilds, so it lights up a frame or
			// two into the level rather than never.
			std::map<ASkyViewpoint *, SkyRgb>::const_iterator got = g_boxDone.find( sky.box );
			if ( got == g_boxDone.end( ))
			{
				bool queued = false;
				for ( size_t q = 0; q < g_boxQueue.size( ); ++q )
					if ( g_boxQueue[q] == sky.box ) { queued = true; break; }
				if ( !queued )
					g_boxQueue.push_back( sky.box );
				continue;
			}

			// Keyed by the actor itself, in its own map. Folding it into the texture-keyed one meant
			// inventing a fake texture index, and every scheme for that either collided with a real
			// texture or changed as the map grew.
			std::map<ASkyViewpoint *, int>::const_iterator seen = boxSourceIdx.find( sky.box );
			int idx;
			if ( seen != boxSourceIdx.end( ))
			{
				idx = seen->second;
			}
			else
			{
				// Derived from the cached RAW colour here, not when the skybox was rendered, so the
				// sliders reach a skybox map the same way they reach a textured one. Same steps in
				// the same order as the texture path below.
				SkySource src;
				src.strengthPct = cl_fua_skytint_strength;
				src.tint = NormaliseBrightness( got->second );

				idx = (int)sources.size( );
				sources.push_back( src );
				boxSourceIdx[sky.box] = idx;
			}

			dist[i] = 0.0;
			litBy[i] = idx;
			queue.push( std::make_pair( 0.0, i ));
			continue;
		}

		const int key = sky.front.GetIndex( );

		std::map<int, int>::const_iterator found = sourceOfTexture.find( key );
		int which = -1;
		if ( found != sourceOfTexture.end( ))
		{
			which = found->second;
		}
		else
		{
			std::vector<SkyRgb> pixels;
			int width = 0;
			if ( !ReadSkyForSector( s, pixels, width ))
			{
				sourceOfTexture[key] = -1;		// remember the failure so we do not retry per leaf
				continue;
			}

			SkySource src;
			const SkyRgb raw = AverageSky( pixels, width );
			SkyRgb tint = raw;

			// The sky says WHICH colour, not how much. What comes out here is a DIRECTION with peak
			// 255; how far to travel along it is EqualiseHuePush's job at apply time.
			src.tint = NormaliseBrightness( tint );

			// [rc4l] The CIELAB solver that used to sit here is gone. It was meant to spend more
			// strength where a tint would show less, but its scene input came from texture albedo,
			// which on Doom maps is brown everywhere: Speed of Doom MAP01/20/29 measured [66,58,52],
			// [83,64,54] and [83,56,40], so it returned 43/42/41% and was a constant with a LAB
			// apparatus bolted on. The real asymmetry was never the scene, it was the hue, and
			// EqualiseHuePush addresses that directly.
			src.strengthPct = cl_fua_skytint_strength;

			g_lastRaw.push_back( raw );
			g_lastTints.push_back( src.tint );
			g_lastStrength.push_back( src.strengthPct );

			which = (int)sources.size( );
			sources.push_back( src );
			sourceOfTexture[key] = which;
		}

		if ( which < 0 )
			continue;

		dist[i] = 0.0;
		litBy[i] = which;
		queue.push( std::make_pair( 0.0, i ));
	}

	if ( sources.empty( ))
		return;

	while ( !queue.empty( ))
	{
		const double at = queue.top( ).first;
		const int here = queue.top( ).second;
		queue.pop( );

		if ( at > dist[here] )
			continue;					// a shorter way here was already found
		if ( at >= reach )
			continue;					// nothing past the limit can be lit

		const subsector_t &sub = subsectors[here];
		for ( DWORD k = 0; k < sub.numlines; ++k )
		{
			const seg_t &seg = sub.firstline[k];
			if (( seg.PartnerSeg == NULL ) || ( seg.PartnerSeg->Subsector == NULL ))
				continue;

			const int ti = (int)( seg.PartnerSeg->Subsector - subsectors );
			if (( ti < 0 ) || ( ti >= numsubsectors ) || ( ti == here ))
				continue;

			const sector_t *to = subsectors[ti].sector;
			if (( to == NULL ) || ( to->ColorMap == NULL ) ||
				(( to->ColorMap->Color.d & 0xFFFFFF ) != 0xFFFFFF ))
			{
				continue;
			}

			const double mx = FIXED2FLOAT(( seg.v1->x / 2 ) + ( seg.v2->x / 2 ));
			const double my = FIXED2FLOAT(( seg.v1->y / 2 ) + ( seg.v2->y / 2 ));

			double factor = 1.0;
			if ( seg.linedef != NULL )
			{
				// A real wall between two sectors: light only crosses a genuine gap, so a closed
				// door stays dark. A seg with no linedef is a BSP cut INSIDE one sector -- there is
				// nothing there to block anything, which is exactly why a room can fade across
				// itself now.
				const sector_t *from = sub.sector;
				const fixed_t fx = FLOAT2FIXED( mx ), fy = FLOAT2FIXED( my );
				const fixed_t openTop = MIN( from->ceilingplane.ZatPoint( fx, fy ), to->ceilingplane.ZatPoint( fx, fy ));
				const fixed_t openBot = MAX( from->floorplane.ZatPoint( fx, fy ), to->floorplane.ZatPoint( fx, fy ));
				if ( openTop <= openBot )
					continue;

				const double opening = FIXED2FLOAT( openTop - openBot );
				const double full = FIXED2FLOAT( MAX(
					from->ceilingplane.ZatPoint( fx, fy ) - from->floorplane.ZatPoint( fx, fy ),
					to->ceilingplane.ZatPoint( fx, fy ) - to->floorplane.ZatPoint( fx, fy )));

				// A half-height gap costs twice the distance. This used to be scaled by a "Doorway
				// matters" dial; it now always counts in full, which is what that dial's default did.
				factor = OpeningFactor( opening, full );
			}

			const double step = StepCost(
				Distance2( cx[here], cy[here], mx, my ) + Distance2( mx, my, cx[ti], cy[ti] ), factor );
			if ( step < 0.0 )
				continue;				// impassable

			const double next = at + step;
			if (( next < dist[ti] ) && ( next < reach ))
			{
				dist[ti] = next;
				litBy[ti] = litBy[here];	// the light carries its own sky with it
				queue.push( std::make_pair( next, ti ));
			}
		}
	}

	g_tint.assign( numsubsectors, PalEntry( 255, 255, 255 ));
	g_dist = dist;		// for the diagnostic; see g_dist's declaration

	for ( int i = 0; i < numsubsectors; ++i )
	{
		// Distance 0 is the open sky itself and is always lit: reach controls how far the light
		// travels INDOORS, so setting it to nothing should mean "outdoors only", not "off".
		if (( dist[i] > 0.0 ) && ( dist[i] >= reach ))
			continue;
		if ( dist[i] >= kInfinity )
			continue;					// never reached at all

		const int src = litBy[i];
		if (( src < 0 ) || ( src >= (int)sources.size( )))
			continue;

		int strength = StrengthAtDistance( sources[src].strengthPct, dist[i], reach );

		// Scaled by how lit this leaf's sector already is. Read per leaf rather than once, because
		// the whole point is that a dark room and a bright yard should differ.
		const sector_t *own = subsectors[i].sector;
		if ( own != NULL )
			strength = StrengthForSectorLight( strength, own->lightlevel, cl_fua_skytint_lit );

		if ( strength <= 0 )
			continue;

		// [rc4l] One call, and no luminance correction afterwards.
		//
		// This used to be PreserveLuminance( BlendFromWhite( tint, strength )), which scaled the
		// result up until its luminance came back to 1.0. That is what made green hit twice as hard
		// as red: green already passes 84% of the light so the correction barely moved it, while red
		// passes 22%, got scaled 4.6x, clamped at 255, and arrived as a faint wash.
		//
		// EqualiseHuePush asks for a channel SPREAD instead, which is the same request for every hue,
		// and solves the blend that delivers it. Nothing is scaled, so nothing clamps.
		//
		// Max colour is a CEILING applied here rather than a cap on the sky's own saturation earlier.
		// Capping the direction first did nothing: the equaliser solves for a target spread, so a
		// desaturated direction simply blended further and arrived at the same place. Capping the
		// REQUEST works, because spread is linear in it -- and because this sits after the distance
		// falloff, it flattens the brightest leaves while leaving the fade into shelter untouched.
		int push = strength;
		if ( push > cl_fua_skytint_saturation )
			push = cl_fua_skytint_saturation;
		const SkyRgb lit = EqualiseHuePush( sources[src].tint, push );
		g_tint[i] = PalEntry( (BYTE)lit.r, (BYTE)lit.g, (BYTE)lit.b );
		g_any = true;
	}

	// Per-sector summary for the callers that know only a sector. "Brightest" by total, so a
	// doorway leaf speaks for its sector rather than the darkest corner of it.
	g_brightestOf.assign( numsectors, PalEntry( 255, 255, 255 ));
	for ( int i = 0; i < numsubsectors; ++i )
	{
		const sector_t *s = subsectors[i].sector;
		if ( s == NULL )
			continue;

		const ptrdiff_t si = s - sectors;
		if (( si < 0 ) || ( si >= (ptrdiff_t)numsectors ))
			continue;

		const PalEntry &mine = g_tint[i];
		PalEntry &best = g_brightestOf[si];
		if (( mine.r + mine.g + mine.b ) < ( best.r + best.g + best.b ))
			best = mine;
	}

	// Only after a build that got this far. Every early return above leaves this invalid on purpose,
	// so a later SkyTint_SkyChanged retries instead of believing a table that was never filled.
	g_builtForSky = sky1texture;
	g_builtForSky2 = sky2texture;
}

namespace
{

// [rc4l] Did a MAPPER colour this sector, as opposed to something the renderer handed us?
//
// This used to be decided by reading cm.LightColor, on the stated assumption that the caller had
// just copied it from the sector's own colormap. That assumption is false wherever 3D floors are
// involved: gl_flats.cpp calls Colormap.CopyLightColor( light->extra_colormap ), which is the 3D
// FLOOR's colour, not the sector's. So on every 3D floor the tint saw a non-white value, concluded a
// mapper had painted the place, and stood aside -- measured on Eon Collection aeon07, where the
// table held [101,101,255] for the leaf at the player start and none of it ever reached the screen.
//
// Asking the SECTOR keeps the reason the check is made at draw time in the first place: ACS
// Sector_SetColor writes sector->ColorMap, and OPEN scripts run after P_SetupLevel has already built
// the table (p_spec.cpp:1835, runNow=false), so a level coloured from ACS is still deferred to.
// How much of the sky light this sector still receives, 0..100. See SkyShareForSectorColour.
int SkyShareOf( const sector_t *sec )
{
	if (( sec == NULL ) || ( sec->ColorMap == NULL ))
		return 100;

	const PalEntry &c = sec->ColorMap->Color;
	return SkyShareForSectorColour( SkyRgb( c.r, c.g, c.b ));
}

// Multiply a stored tint into whatever the surface already had.
void ApplyIndex( ptrdiff_t at, FColormap &cm, const sector_t *owner )
{
	if (( at < 0 ) || ( at >= (ptrdiff_t)g_tint.size( )))
		return;

	const PalEntry &tint = g_tint[at];
	if ( tint.r == 255 && tint.g == 255 && tint.b == 255 )
		return;

	// A sector the mapper coloured keeps its own character by taking less of the sky, rather than
	// all or nothing. Faded toward white, which is the neutral multiplier, so share 0 is untouched.
	const int share = SkyShareOf( owner );
	if ( share <= 0 )
		return;
	const PalEntry shared(
		(BYTE)( 255 - ((( 255 - tint.r ) * share ) / 100 )),
		(BYTE)( 255 - ((( 255 - tint.g ) * share ) / 100 )),
		(BYTE)( 255 - ((( 255 - tint.b ) * share ) / 100 )));

	// [rc4l] TRIPWIRE, not a live guard: nothing in this tree sets blendfactor today. It is the
	// sector colormap's alpha, and every producer passes alpha 0 (Static_Init uses MAKERGB, UDMF
	// uses PalEntry(r,g,b), ACS Sector_SetColor takes r/g/b). Boom's own colormaps do NOT arrive
	// here at all: Transfer_Heights writes bottommap/midmap/topmap, which is a viewer screen blend.
	//
	// Kept because if a port ever does light it up, a non-zero blendfactor sends gl_CalcLightColor
	// down a path that mixes the colour in at a FIXED factor regardless of light level, and
	// multiplying into that would darken an authored colour rather than light anything.
	if ( cm.blendfactor != 0 )
		return;

	// Multiplied into whatever the surface already had rather than replacing it: the tint is light
	// arriving from outside, not a repaint, so a coloured sector keeps its own character.
	cm.LightColor.r = (BYTE)(( cm.LightColor.r * shared.r ) / 255 );
	cm.LightColor.g = (BYTE)(( cm.LightColor.g * shared.g ) / 255 );
	cm.LightColor.b = (BYTE)(( cm.LightColor.b * shared.b ) / 255 );
}

} // namespace

size_t SkyTintTableSize( )
{
	return g_tint.size( );
}

// [rc4l] How many leaves actually carry colour, as opposed to how many the table has room for.
// "table=3035 any=1" says a table exists and something in it is lit; it does not say whether that is
// most of the level or one sector, and those two look identical from inside the game.
size_t SkyTintLitLeaves( )
{
	size_t n = 0;
	for ( size_t i = 0; i < g_tint.size( ); ++i )
	{
		const PalEntry &t = g_tint[i];
		if (( t.r != 255 ) || ( t.g != 255 ) || ( t.b != 255 ))
			++n;
	}
	return n;
}

int SkyTintSkyboxLeaves( )
{
	return g_skyboxLeaves;
}

bool SkyTintOutdoorSpot( double &x, double &y )
{
	if ( !g_lookAtValid )
		return false;

	x = g_lookAtX;
	y = g_lookAtY;
	return true;
}

void SkyTintHere( std::string &out )
{
	out.clear( );

	AActor *mo = ( consoleplayer >= 0 && consoleplayer < MAXPLAYERS ) ? players[consoleplayer].mo : NULL;
	if (( mo == NULL ) || ( subsectors == NULL ) || ( gamestate != GS_LEVEL ))
	{
		out = "no player";
		return;
	}

	// The leaf under the player's feet, found the same way the renderer finds one, so this reports
	// what would actually be drawn rather than a nearby guess.
	const subsector_t *sub = R_PointInSubsector( mo->x, mo->y );
	if ( sub == NULL )
	{
		out = "no leaf";
		return;
	}

	char buf[192];
	const ptrdiff_t li = sub - subsectors;
	const sector_t *sec = sub->sector;
	const ptrdiff_t si = ( sec != NULL ) ? ( sec - sectors ) : -1;

	const bool haveLeaf = ( li >= 0 ) && ( li < (ptrdiff_t)g_tint.size( ));
	const PalEntry leaf = haveLeaf ? g_tint[li] : PalEntry( 255, 255, 255 );
	const bool haveSec = ( si >= 0 ) && ( si < (ptrdiff_t)g_brightestOf.size( ));
	const PalEntry secTint = haveSec ? g_brightestOf[si] : PalEntry( 255, 255, 255 );

	// ffloors is the whole point of this line: it says whether the spot being complained about is
	// even one of the 3D floor cases, rather than an ordinary sector that happens to look like one.
	const int nff = ( sec != NULL && sec->e != NULL ) ? (int)sec->e->XFloor.ffloors.Size( ) : 0;

	// [rc4l] Does this sector SEE sky, by the same test the seeding pass uses? An untinted leaf whose
	// ceiling is the sky flat is a seeding bug; an untinted leaf that never sees sky is just indoors
	// and too far from a way out. Those two need opposite fixes and look identical from in-game.
	const int ceilSky = ( sec != NULL && sec->GetTexture( sector_t::ceiling ) == skyflatnum ) ? 1 : 0;
	const int floorSky = ( sec != NULL && sec->GetTexture( sector_t::floor ) == skyflatnum ) ? 1 : 0;

	// [rc4l] The sector's OWN colour, because a non-white one excludes it from seeding entirely -- it
	// gets no tint and does not light its neighbours either. That rule is invisible from in-game and
	// looks exactly like a propagation failure.
	const PalEntry own = ( sec != NULL && sec->ColorMap != NULL ) ? sec->ColorMap->Color : PalEntry( 255, 255, 255 );

	// The propagation distance, which separates "no open path to sky" from "further than reach
	// allows". Raising the reach and watching nothing change points at the first but does not prove
	// it: a miscomputed opening would look identical from outside.
	char distText[32];
	if (( li < 0 ) || ( li >= (ptrdiff_t)g_dist.size( )))
		mysnprintf( distText, sizeof( distText ), "?" );
	else if ( g_dist[li] >= kSkyTintInfinity )
		mysnprintf( distText, sizeof( distText ), "UNREACHABLE" );
	else
		mysnprintf( distText, sizeof( distText ), "%d", (int)( g_dist[li] + 0.5 ));

	mysnprintf( buf, sizeof( buf ), "leaf=%d sector=%d ffloors=%d ceilsky=%d floorsky=%d dist=%s own[%d,%d,%d] leaftint[%d,%d,%d] sectortint[%d,%d,%d]",
		(int)li, (int)si, nff, ceilSky, floorSky, distText, own.r, own.g, own.b,
		leaf.r, leaf.g, leaf.b, secTint.r, secTint.g, secTint.b );
	out = buf;
}

void SkyTintDerived( std::string &out )
{
	out.clear( );
	if ( g_lastTints.empty( ))
	{
		out = "none";
		return;
	}

	char buf[128];
	for ( size_t i = 0; i < g_lastTints.size( ); ++i )
	{
		const SkyRgb &raw = g_lastRaw[i];
		const SkyRgb &t = g_lastTints[i];

		// Saturation as (max-min)/max, the same definition ClampSaturation uses. Printed because it
		// is the number that says how hard this sky will push a neutral wall away from grey.
		// Written out rather than using MAX or std::max. The engine's MAX resolves to its fixed-point
		// type here and will not convert back to int, and windows.h (via gl_system.h) defines max as a
		// MACRO, which makes std::max fail to parse. Three comparisons are not worth fighting either.
		int mx = t.r; if ( t.g > mx ) mx = t.g; if ( t.b > mx ) mx = t.b;
		int mn = t.r; if ( t.g < mn ) mn = t.g; if ( t.b < mn ) mn = t.b;
		const int sat = ( mx <= 0 ) ? 0 : ((( mx - mn ) * 100 ) / mx );

		const int str = ( i < g_lastStrength.size( )) ? g_lastStrength[i] : -1;
		mysnprintf( buf, sizeof( buf ), "raw[%d,%d,%d]->tint[%d,%d,%d] sat=%d%% solved=%d%% ",
			raw.r, raw.g, raw.b, t.r, t.g, t.b, sat, str );
		out += buf;
	}

	mysnprintf( buf, sizeof( buf ), "| scene[%d,%d,%d]", g_lastAlbedo.r, g_lastAlbedo.g,
		g_lastAlbedo.b );
	out += buf;
}

// TEMPORARY, for fua_skytintinfo: what colour did each sampled skybox come out as, and how hard
// does it push. Without this, "the table is built" and "the table does anything" look identical.
void SkyTintSkyboxSamples( std::string &out )
{
	out.clear( );
	if ( g_boxDone.empty( ))
	{
		out = ( g_boxQueue.empty( ) && ( g_boxPendingFor == NULL )) ? "none" : "pending";
		return;
	}

	// The RAW sample, which is what is cached. The finished tint is derived from this per rebuild, so
	// printing that instead would only echo the current sliders back.
	char buf[96];
	for ( std::map<ASkyViewpoint *, SkyRgb>::const_iterator it = g_boxDone.begin( );
		it != g_boxDone.end( ); ++it )
	{
		mysnprintf( buf, sizeof( buf ), "raw[%d,%d,%d] ", it->second.r, it->second.g, it->second.b );
		out += buf;
	}
}

void SkyTint_FrameHook( )
{
	// Called from gl_scene straight after FCanvasTextureInfo::UpdateAll, so anything registered on a
	// previous frame has just been rendered and is ready to read.
	if ( !cl_fua_skytint || ( NETWORK_GetState( ) == NETSTATE_SERVER ))
		return;
	if (( g_boxPendingFor == NULL ) && g_boxQueue.empty( ))
		return;
	if ( gamestate != GS_LEVEL )
		return;

	// A viewpoint registered last frame: try to read what was rendered for it.
	if ( g_boxPendingFor != NULL )
	{
		std::vector<SkyRgb> pixels;
		int width = 0;
		if ( !ReadCanvas( pixels, width ))
			return;			// not rendered yet; try again next frame

		// [rc4l] The RAW average is cached, not a finished colour.
		//
		// Caching the finished article froze strength and saturation at whatever they were the moment
		// the skybox happened to be rendered. A rebuild then reused that verbatim, so on a skybox map
		// every slider in the menu did nothing: measured on gvh09 as an identical [255,29,29 str=100]
		// whether the sliders said 35/60 or 100/100. Rendering the skybox is the expensive part and
		// still happens once; turning pixels into a colour is cheap and now happens per rebuild, the
		// same as it does for a texture sky.
		g_boxDone[g_boxPendingFor] = AverageSky( pixels, width );
		g_boxPendingFor = NULL;

		// The table was built without this sky. Rebuild so the leaves under it stop being dark.
		SkyTint_Rebuild( );
		return;
	}

	// Nothing in flight: register the next one and let UpdateAll draw it on the following frame.
	ASkyViewpoint *box = g_boxQueue.back( );
	g_boxQueue.pop_back( );
	if ( box == NULL )
		return;

	if ( g_boxCanvas == NULL )
	{
		g_boxCanvas = new FCanvasTexture( "__fua_skysample", kBoxSampleSize, kBoxSampleSize );
		g_boxCanvasId = TexMan.AddTexture( g_boxCanvas );
	}
	if ( !g_boxCanvasId.isValid( ))
		return;

	g_boxCanvas->NeedUpdate( );
	FCanvasTextureInfo::Add( box, g_boxCanvasId, 90 );
	g_boxPendingFor = box;
}

bool SkyTint_Active( )
{
	return g_any;
}

void SkyTint_ApplySub( const subsector_t *sub, FColormap &cm )
{
	if ( !g_any || ( sub == NULL ) || ( subsectors == NULL ))
		return;

	ApplyIndex( sub - subsectors, cm, sub->sector );
}

void SkyTint_Apply( const sector_t *sec, FColormap &cm )
{
	if ( !g_any || ( sec == NULL ) || ( sectors == NULL ))
		return;

	// [rc4l] Only a few callers know just the sector: sprites away from a leaf, horizon portals.
	// Light lives per subsector now, so pick this sector's BRIGHTEST leaf -- erring toward the lit
	// side keeps a thing standing in a doorway from going dark, which reads worse than the reverse.
	//
	// gl_FakeFlat hands out pointers to STACK copies as well as to the real array, and this used to
	// give up whenever the pointer was not inside `sectors`. That silently dropped the tint in
	// exactly the case a copy is made for: standing above or below a 3D floor, which is when the
	// renderer swaps in a fake sector. On a map built out of them the effect simply vanished.
	//
	// The copies are whole-struct (`*dest = *sec` / memcpy in gl_fakeflat.cpp), so they carry
	// `sectornum`, which r_defs.h keeps expressly "for comparing sector copies". Ask the sector which
	// one it is instead of inferring it from where it happens to live.
	ptrdiff_t si = sec - sectors;
	if (( si < 0 ) || ( si >= (ptrdiff_t)numsectors ))
		si = sec->sectornum;
	if (( si < 0 ) || ( si >= (ptrdiff_t)numsectors ))
		return;

	const PalEntry &tint = g_brightestOf[si];
	if ( tint.r == 255 && tint.g == 255 && tint.b == 255 )
		return;

	// Same rule the per-leaf path uses, decided from the sector rather than from cm: this is called
	// after gl_flats.cpp has already folded in a 3D floor's light colour, so testing cm here would
	// stand aside on every 3D floor. `sectors[si]` rather than `sec`, because `sec` may be a
	// gl_FakeFlat stack copy whose ColorMap pointer is the copy's, not the level's.
	const int share = SkyShareOf( &sectors[si] );
	if ( share <= 0 )
		return;
	const PalEntry shared(
		(BYTE)( 255 - ((( 255 - tint.r ) * share ) / 100 )),
		(BYTE)( 255 - ((( 255 - tint.g ) * share ) / 100 )),
		(BYTE)( 255 - ((( 255 - tint.b ) * share ) / 100 )));

	// Multiplied into whatever the sector already had rather than replacing it: the tint is light
	// arriving from outside, not a repaint, so a coloured sector keeps its own character.
	cm.LightColor.r = (BYTE)(( cm.LightColor.r * shared.r ) / 255 );
	cm.LightColor.g = (BYTE)(( cm.LightColor.g * shared.g ) / 255 );
	cm.LightColor.b = (BYTE)(( cm.LightColor.b * shared.b ) / 255 );
}

} // namespace zx
