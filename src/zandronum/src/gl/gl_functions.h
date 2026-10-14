#ifndef __GL_FUNCT
#define __GL_FUNCT

#include "v_palette.h"

class AActor;

void gl_PreprocessLevel();
void gl_CleanLevelData();

// [BB] Export this.
bool IsLightmodeValid();
// [BB] This construction purposely overrides the CVAR r_fogmode with a local variable of the same name.
// This allows to implement ZADF_FORCE_VIDEO_DEFAULTS by only putting this define at the beginning of a function
// that uses r_fogmode without any further changes in that function.
// Note: r_fogmode == 0 is treated as if r_fogmode == 1.
#define OVERRIDE_FOGMODE_IF_NECESSARY \
	const int gl_fogmode_CVAR_value = r_fogmode; \
	const int r_fogmode = ( ( zadmflags & ZADF_FORCE_VIDEO_DEFAULTS ) && ( gl_fogmode_CVAR_value == 0 ) ) ? 1 : gl_fogmode_CVAR_value;

// [EP] Override r_light_ambient in the same way as r_fogmode.
#define TEMP_CVAR( cvar ) cvar ## _CVAR_value
#define TEMP_DEFAULT_CVAR( cvar ) cvar ## _CVAR_defaultvalue

#define OVERRIDE_INT_GL_CVAR_IF_NECESSARY( cvar ) \
	const int TEMP_CVAR( cvar ) = cvar; \
	const int TEMP_DEFAULT_CVAR( cvar ) = cvar.GetGenericRepDefault( CVAR_Int ).Int; \
	const int cvar = ( zadmflags & ZADF_FORCE_VIDEO_DEFAULTS ) ? TEMP_DEFAULT_CVAR( cvar ) : TEMP_CVAR( cvar )


#endif
