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

// [rc4l] How much the sky's OWN brightness scales the tint. 0 keeps hue only, so a dim sky tints
// as hard as a blazing one -- which is why a dark green sky can read as a filter over a whole map.
// Higher lets each map's sky limit itself, which the Max colour slider cannot do because it is
// global: turning that down to tame one map costs the tint on every map where it was already fine.
CUSTOM_CVAR( Int, cl_fua_skytint_brightness, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )
{
	if ( self < 0 )				self = 0;
	else if ( self > 100 )		self = 100;
	else						zx::SkyTint_Rebuild( );
}

// [rc4l] How much a sector's OWN light level scales the tint. This is the dial that tells a dark
// room from a bright yard, which no sky-side control can: the sky is the same for both. A dim
// sector showing little of the light's colour is also just what light does.
CUSTOM_CVAR( Int, cl_fua_skytint_lit, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )
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
std::vector<PalEntry> g_tint;			// per SUBSECTOR: what the renderer actually draws
std::vector<PalEntry> g_brightestOf;	// per SECTOR: its lightest leaf, for the few sector-only callers
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
std::map<ASkyViewpoint *, SkySource> g_boxDone;

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
	Printf( "skytint: sky1texture=%d tex=%p w=%d h=%d skyflatnum=%d\n",
		sky1texture.GetIndex( ), (void *)sky,
		sky ? sky->GetWidth( ) : -1, sky ? sky->GetHeight( ) : -1, skyflatnum.GetIndex( ));
	Printf( "skytint: table=%u any=%d skyboxleaves=%d doublesky=%d swapskies=%d sky2=%d\n",
		(unsigned)zx::SkyTintTableSize( ), (int)zx::SkyTint_Active( ), zx::SkyTintSkyboxLeaves( ),
		(int)!!( level.flags & LEVEL_DOUBLESKY ), (int)!!( level.flags & LEVEL_SWAPSKIES ),
		sky2texture.GetIndex( ));

	std::string boxes;
	zx::SkyTintSkyboxSamples( boxes );
	Printf( "skytint: skyboxes=%s\n", boxes.c_str( ));
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

	const SkyAverage mode = ( cl_fua_skytint_mode == 1 ) ? SkyAverage::Dominant : SkyAverage::Mean;
	const SkyWeight weight = ( cl_fua_skytint_weight == 1 ) ? SkyWeight::Cosine : SkyWeight::Horizon;

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
	const double kInfinity = 1e30;

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

	for ( int i = 0; i < numsubsectors; ++i )
	{
		const sector_t *s = subsectors[i].sector;
		if (( s == NULL ) || ( s->GetTexture( sector_t::ceiling ) != skyflatnum ))
			continue;
		// A mapper or mod that coloured this sector meant it; theirs wins and we never touch it.
		if (( s->ColorMap == NULL ) || (( s->ColorMap->Color.d & 0xFFFFFF ) != 0xFFFFFF ))
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
			std::map<ASkyViewpoint *, SkySource>::const_iterator got = g_boxDone.find( sky.box );
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
				idx = (int)sources.size( );
				sources.push_back( got->second );
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
			SkyRgb tint = AverageSky( pixels, width, mode, weight );

			// Measured BEFORE the brightness is normalised away, because afterwards there is none
			// left to read. This is what lets a dark sky tint gently and a bright one tint hard.
			src.strengthPct = StrengthForSky( cl_fua_skytint_strength, SkyLuminance( tint ),
				cl_fua_skytint_brightness );

			tint = NormaliseBrightness( tint );			// the sky says WHICH colour, not how much
			src.tint = ClampSaturation( tint, cl_fua_skytint_saturation );

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

	const double blend = cl_fua_skytint_gap / 100.0;

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

				// cl_fua_skytint_gap: at 0 every opening counts as wide open, at 100 a half-height
				// gap costs twice the distance.
				const double raw = OpeningFactor( opening, full );
				factor = ( raw * blend ) + ( 1.0 - blend );
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

		const SkyRgb lit = BlendFromWhite( sources[src].tint, strength );
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

// Multiply a stored tint into whatever the surface already had.
void ApplyIndex( ptrdiff_t at, FColormap &cm )
{
	if (( at < 0 ) || ( at >= (ptrdiff_t)g_tint.size( )))
		return;

	const PalEntry &tint = g_tint[at];
	if ( tint.r == 255 && tint.g == 255 && tint.b == 255 )
		return;

	// [rc4l] "A mapper coloured this, theirs wins" has to be decided HERE, not only when the table is
	// built, because the two happen at different times. P_SetupLevel builds the table at its line
	// 4672, but ACS OPEN scripts are started with runNow=false (p_spec.cpp:1835) and therefore do not
	// run until the first tic. A Sector_SetColor in an OPEN script -- which is how a lot of mods
	// colour their levels -- lands after we have already decided to tint that sector, and the tint
	// then multiplied into their colour instead of deferring to it.
	//
	// Reading cm rather than the sector costs nothing: cm.LightColor was just copied from the
	// sector's own colormap by the caller, so a non-white value here means somebody set it, whenever
	// they set it. That makes the rule true at every moment rather than only at level load.
	if (( cm.LightColor.r != 255 ) || ( cm.LightColor.g != 255 ) || ( cm.LightColor.b != 255 ))
		return;

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
	cm.LightColor.r = (BYTE)(( cm.LightColor.r * tint.r ) / 255 );
	cm.LightColor.g = (BYTE)(( cm.LightColor.g * tint.g ) / 255 );
	cm.LightColor.b = (BYTE)(( cm.LightColor.b * tint.b ) / 255 );
}

} // namespace

size_t SkyTintTableSize( )
{
	return g_tint.size( );
}

int SkyTintSkyboxLeaves( )
{
	return g_skyboxLeaves;
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

	char buf[96];
	for ( std::map<ASkyViewpoint *, SkySource>::const_iterator it = g_boxDone.begin( );
		it != g_boxDone.end( ); ++it )
	{
		mysnprintf( buf, sizeof( buf ), "[%d,%d,%d str=%d] ", it->second.tint.r, it->second.tint.g,
			it->second.tint.b, it->second.strengthPct );
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

		const SkyAverage mode = ( cl_fua_skytint_mode == 1 ) ? SkyAverage::Dominant : SkyAverage::Mean;

		// Weighting is NOT applied here. Row weights describe where a row sits on the sky dome, and
		// this is a camera frame of a room, not a dome: its rows mean nothing of the kind. Every
		// pixel counts equally, which is the honest reading of "what is up there".
		SkyRgb tint = AverageSky( pixels, width, mode, SkyWeight::Cosine );

		SkySource src;
		src.strengthPct = StrengthForSky( cl_fua_skytint_strength, SkyLuminance( tint ),
			cl_fua_skytint_brightness );
		tint = NormaliseBrightness( tint );
		src.tint = ClampSaturation( tint, cl_fua_skytint_saturation );

		g_boxDone[g_boxPendingFor] = src;
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

	ApplyIndex( sub - subsectors, cm );
}

void SkyTint_Apply( const sector_t *sec, FColormap &cm )
{
	if ( !g_any || ( sec == NULL ) || ( sectors == NULL ))
		return;

	// [rc4l] Only a few callers know just the sector: sprites away from a leaf, horizon portals.
	// Light lives per subsector now, so pick this sector's BRIGHTEST leaf -- erring toward the lit
	// side keeps a thing standing in a doorway from going dark, which reads worse than the reverse.
	//
	// gl_FakeFlat hands out pointers to STACK copies as well as to the real array, so a sector that
	// does not live in the level's array has no leaves to look up and is left alone.
	const ptrdiff_t si = sec - sectors;
	if (( si < 0 ) || ( si >= (ptrdiff_t)numsectors ))
		return;

	const PalEntry &tint = g_brightestOf[si];
	if ( tint.r == 255 && tint.g == 255 && tint.b == 255 )
		return;

	// Multiplied into whatever the sector already had rather than replacing it: the tint is light
	// arriving from outside, not a repaint, so a coloured sector keeps its own character.
	cm.LightColor.r = (BYTE)(( cm.LightColor.r * tint.r ) / 255 );
	cm.LightColor.g = (BYTE)(( cm.LightColor.g * tint.g ) / 255 );
	cm.LightColor.b = (BYTE)(( cm.LightColor.b * tint.b ) / 255 );
}

} // namespace zx
