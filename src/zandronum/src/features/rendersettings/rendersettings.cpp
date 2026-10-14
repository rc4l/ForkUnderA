// [rc4l] The render settings' storage, moved from GL's files so a launch or build without GL has them too.

#include "features/rendersettings/rendersettings.h"
#include "features/rendersettings/computation/forcedvideo_compute.h"
#include "features/levelmesh/computation/shading_compute.h"   // [rc4l] ShadingEnv
#include "doomstat.h"   // [rc4l] zadmflags
#include "g_level.h"    // [rc4l] level.flags
#include "d_player.h"   // [rc4l] P_ResetPlayerPitchLimits

#include <string.h>   // [rc4l] memset, clearing the map sections

GLRenderSettings glset;
long gl_frameMS;
long gl_frameCount;

int fogdensity;
int outsidefogdensity;
int skyfog;

DWORD gl_fixedcolormap;
area_t in_area;
TArray<BYTE> currentmapsection;

void ClearMapSections()
{
	if (currentmapsection.Size() > 0) memset(&currentmapsection[0], 0, currentmapsection.Size());
}

void MarkMapSectionBit(int section)
{
	currentmapsection[section >> 3] |= 1 << (section & 7);
}

MapSectionScope::MapSectionScope() : saved(currentmapsection) { ClearMapSections(); }
MapSectionScope::~MapSectionScope() { currentmapsection = saved; }

CVAR(Int, r_weaponlight, 8, CVAR_ARCHIVE);

// [rc4l] Settings both renderers read, defined here rather than in gl/ so a build without GL (the server) still links them.
CVAR(Bool, r_mirrors, true, 0)	// This is for debugging only!
CVAR(Bool, r_mirror_envmap, true, CVAR_GLOBALCONFIG|CVAR_ARCHIVE)
CVAR(Bool, r_seamless, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CUSTOM_CVAR(Int, r_mirror_recursions, 4, CVAR_GLOBALCONFIG|CVAR_ARCHIVE)
{
	if (self<0) self=0;
	if (self>10) self=10;
}
bool gl_plane_reflection_i;	// This is needed in a header that cannot include the CVAR stuff...
CUSTOM_CVAR(Bool, gl_plane_reflection, true, CVAR_GLOBALCONFIG|CVAR_ARCHIVE)
{
	gl_plane_reflection_i = self;
}
CVAR(Bool, r_brightfog, false, CVAR_ARCHIVE);
CVAR(Bool, r_usecolorblending, true, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR(Bool, r_sprite_blend, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG);
CVAR(Int, r_spriteclip, 1, CVAR_ARCHIVE)
CVAR(Float, r_sclipthreshold, 10.0, CVAR_ARCHIVE)
CVAR(Float, r_sclipfactor, 1.8, CVAR_ARCHIVE)
CVAR(Int, r_particles_style, 2, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) // 0 = square, 1 = round, 2 = smooth
CVAR(Int, r_billboard_mode, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, r_billboard_particles, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Int, r_enhanced_nv_stealth, 3, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CUSTOM_CVAR(Int, r_fuzztype, 0, CVAR_ARCHIVE)
{
	if (self < 0 || self > 7) self = 0;
}
CVAR(Bool, r_noskyboxes, false, 0)
CVAR(Bool, gl_render_things, true, CVAR_DEBUGONLY)
// [BB]
CUSTOM_CVAR( Bool, cl_disallowfullpitch, false, CVAR_ARCHIVE )
{
	// [AK] Reimpose the new pitch limits for all active players.
	P_ResetPlayerPitchLimits();
}

// [rc4l] Whether the server forces video defaults (ZADF_FORCE_VIDEO_DEFAULTS).
static bool VideoDefaultsForced()
{
	return (zadmflags & ZADF_FORCE_VIDEO_DEFAULTS) != 0;
}

// [rc4l] OVERRIDE_INT_GL_CVAR_IF_NECESSARY: an int setting as read under the server's forced defaults.
static int ServerForcedInt(FIntCVar &cvar)
{
	return zx::rendersettings::ComputeForcedVideoInt(VideoDefaultsForced(), *cvar, cvar.GetGenericRepDefault(CVAR_Int).Int);
}

float distfogtable[2][256];	// light to fog conversion table for black fog
PalEntry outsidefogcolor;

CUSTOM_CVAR (Int, r_light_ambient, 20, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	// ambient of 0 does not work correctly because light level 0 is special.
	if (self < 1) self = 1;
}

CUSTOM_CVAR (Int, r_distfog, 70, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	// [BB/EP] Take care of r_distfog and ZADF_FORCE_VIDEO_DEFAULTS.
	const int r_distfog = ServerForcedInt(self);

	for (int i=0;i<256;i++)
	{

		if (i<164)
		{
			distfogtable[0][i]= (r_distfog>>1) + (r_distfog)*(164-i)/164;
		}
		else if (i<230)
		{											    
			distfogtable[0][i]= (r_distfog>>1) - (r_distfog>>1)*(i-164)/(230-164);
		}
		else distfogtable[0][i]=0;

		if (i<128)
		{
			distfogtable[1][i]= 6.f + (r_distfog>>1) + (r_distfog)*(128-i)/48;
		}
		else if (i<216)
		{											    
			distfogtable[1][i]= (216.f-i) / ((216.f-128.f)) * r_distfog / 10;
		}
		else distfogtable[1][i]=0;
	}
}

CUSTOM_CVAR(Int,r_fogmode,1,CVAR_ARCHIVE|CVAR_NOINITCALL)
{
	if (self>2) self=2;
	if (self<0) self=0;
}

void gl_SetFogParams(int _fogdensity, PalEntry _outsidefogcolor, int _outsidefogdensity, int _skyfog)
{
	fogdensity=_fogdensity;
	outsidefogcolor=_outsidefogcolor;
	outsidefogdensity=_outsidefogdensity;
	skyfog=_skyfog;

	outsidefogdensity>>=1;
	fogdensity>>=1;
}


zx::levelmesh::ShadingEnv RenderShadingEnv(bool inSkybox)
{
	const int r_light_ambient = ServerForcedInt(::r_light_ambient);
	const int r_distfog = ServerForcedInt(::r_distfog);
	// [rc4l] As OVERRIDE_FOGMODE_IF_NECESSARY, forced defaults never turn fog off.
	const int r_fogmode = zx::rendersettings::ComputeForcedFogMode(VideoDefaultsForced(), ::r_fogmode);
	zx::levelmesh::ShadingEnv e;
	e.lightMode = glset.lightmode;
	e.ambient = r_light_ambient;
	e.fogMode = r_fogmode;
	e.distFog = r_distfog;
	e.fixedColormap = gl_fixedcolormap != CM_DEFAULT;
	e.fadeTable = (level.flags & LEVEL_HASFADETABLE) != 0;
	e.inSkybox = inSkybox;
	e.brightFog = glset.brightfog;
	e.fogDensity = fogdensity;
	e.outsideFogDensity = outsidefogdensity;
	e.outsideFogColor = outsidefogcolor.d;
	return e;
}

