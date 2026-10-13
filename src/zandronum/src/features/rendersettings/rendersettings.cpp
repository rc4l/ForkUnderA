// [rc4l] The render settings' storage, moved from GL's files so a launch or build without GL has them too.

#include "features/rendersettings/rendersettings.h"
#include "features/rendersettings/computation/forcedvideo_compute.h"
#include "features/levelmesh/computation/shading_compute.h"   // [rc4l] ShadingEnv
#include "doomstat.h"   // [rc4l] zadmflags
#include "g_level.h"    // [rc4l] level.flags

GLRenderSettings glset;
long gl_frameMS;

int fogdensity;
int outsidefogdensity;
int skyfog;

DWORD gl_fixedcolormap;
area_t in_area;
TArray<BYTE> currentmapsection;

CVAR(Int, gl_weaponlight, 8, CVAR_ARCHIVE);

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

CUSTOM_CVAR (Int, gl_light_ambient, 20, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	// ambient of 0 does not work correctly because light level 0 is special.
	if (self < 1) self = 1;
}

CUSTOM_CVAR (Int, gl_distfog, 70, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	// [BB/EP] Take care of gl_distfog and ZADF_FORCE_VIDEO_DEFAULTS.
	const int gl_distfog = ServerForcedInt(self);

	for (int i=0;i<256;i++)
	{

		if (i<164)
		{
			distfogtable[0][i]= (gl_distfog>>1) + (gl_distfog)*(164-i)/164;
		}
		else if (i<230)
		{											    
			distfogtable[0][i]= (gl_distfog>>1) - (gl_distfog>>1)*(i-164)/(230-164);
		}
		else distfogtable[0][i]=0;

		if (i<128)
		{
			distfogtable[1][i]= 6.f + (gl_distfog>>1) + (gl_distfog)*(128-i)/48;
		}
		else if (i<216)
		{											    
			distfogtable[1][i]= (216.f-i) / ((216.f-128.f)) * gl_distfog / 10;
		}
		else distfogtable[1][i]=0;
	}
}

CUSTOM_CVAR(Int,gl_fogmode,1,CVAR_ARCHIVE|CVAR_NOINITCALL)
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
	const int gl_light_ambient = ServerForcedInt(::gl_light_ambient);
	const int gl_distfog = ServerForcedInt(::gl_distfog);
	// [rc4l] As OVERRIDE_FOGMODE_IF_NECESSARY, forced defaults never turn fog off.
	const int gl_fogmode = zx::rendersettings::ComputeForcedFogMode(VideoDefaultsForced(), ::gl_fogmode);
	zx::levelmesh::ShadingEnv e;
	e.lightMode = glset.lightmode;
	e.ambient = gl_light_ambient;
	e.fogMode = gl_fogmode;
	e.distFog = gl_distfog;
	e.fixedColormap = gl_fixedcolormap != CM_DEFAULT;
	e.fadeTable = (level.flags & LEVEL_HASFADETABLE) != 0;
	e.inSkybox = inSkybox;
	e.brightFog = glset.brightfog;
	e.fogDensity = fogdensity;
	e.outsideFogDensity = outsidefogdensity;
	e.outsideFogColor = outsidefogcolor.d;
	return e;
}

