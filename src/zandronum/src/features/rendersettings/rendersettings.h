// [rc4l] The level's render settings both renderers read: MAPINFO's light mode and fog, the weapon light, the view's area and map sections, and the frame clock; moved here from gl/data/gl_data.h, gl/renderer/gl_lightdata.h and gl/renderer/gl_renderer.h, which include it.

#ifndef ZX_RENDERSETTINGS_H
#define ZX_RENDERSETTINGS_H

#include "doomtype.h"
#include "tarray.h"
#include "vectors.h"
#include "c_cvars.h"
#include "features/rendersettings/colormap.h"

struct GLRenderSettings
{

	SBYTE lightmode;
	bool nocoloredspritelighting;
	bool notexturefill;
	bool brightfog;

	SBYTE map_lightmode;
	SBYTE map_nocoloredspritelighting;
	SBYTE map_notexturefill;
	SBYTE map_brightfog;

	FVector3 skyrotatevector;
	FVector3 skyrotatevector2;

	float pixelstretch;

};

extern GLRenderSettings glset;

// [rc4l] MAPINFO's fog densities and the sky's fog strength, the outside fog's colour, and black fog's distance table, which r_distfog rebuilds.
extern int fogdensity;
extern int outsidefogdensity;
extern int skyfog;
extern PalEntry outsidefogcolor;
extern float distfogtable[2][256];
void gl_SetFogParams(int _fogdensity, PalEntry _outsidefogcolor, int _outsidefogdensity, int _skyfog);

EXTERN_CVAR(Int, r_light_ambient)
EXTERN_CVAR(Int, r_distfog)
EXTERN_CVAR(Int, r_fogmode)

// [rc4l] What the light model reads besides the surface (light mode, ambient, fog rules, fixed colormap), the server's forced defaults applied; `inSkybox` says the surface is drawn inside a skybox.
namespace zx { namespace levelmesh { struct ShadingEnv; } }
zx::levelmesh::ShadingEnv RenderShadingEnv(bool inSkybox);

// [rc4l] Which side of a height transfer's planes the view is on.
typedef enum
{
        area_normal,
        area_below,
        area_above,
		area_default
} area_t;

extern area_t			in_area;

// [rc4l] The map sections the view's walks may enter.
extern TArray<BYTE> currentmapsection;

// [rc4l] The frame's clock in milliseconds, which warps and glows run on.
extern long gl_frameMS;
// [rc4l] How many frames have been drawn, counted once a frame by the player view.
extern long gl_frameCount;

extern int extralight;
EXTERN_CVAR(Int, r_weaponlight);

inline	int getExtraLight()
{
	return extralight * r_weaponlight;
}

inline int gl_ClampLight(int lightlevel)
{
	return lightlevel < 0 ? 0 : lightlevel > 255 ? 255 : lightlevel;
}

inline bool gl_isBlack(PalEntry color)
{
	return color.r + color.g + color.b == 0;
}

inline bool gl_isWhite(PalEntry color)
{
	return color.r + color.g + color.b == 3*0xff;
}

#endif // ZX_RENDERSETTINGS_H
