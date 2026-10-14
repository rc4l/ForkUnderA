

#include "gl/system/gl_system.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "v_video.h"
#include "version.h"
#include "gl/system/gl_interface.h"
#include "gl/system/gl_cvars.h"
#include "gl/renderer/gl_renderer.h"
#include "menu/menu.h"



// OpenGL stuff moved here
// GL related CVARs
CVAR(Bool, gl_portals, true, 0)
CVAR(Bool, gl_noquery, false, 0)
EXTERN_CVAR(Bool, r_seamless)

CUSTOM_CVAR(Bool, gl_render_precise, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	r_seamless=self;
}

CUSTOM_CVAR (Float, vid_brightness, 0.f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	if (screen != NULL)
	{
		screen->SetGamma(Gamma); //Brightness (self);
	}
}

CUSTOM_CVAR (Float, vid_contrast, 1.f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	if (screen != NULL)
	{
		screen->SetGamma(Gamma); //SetContrast (self);
	}
}


// Do some tinkering with the menus so that certain options only appear
// when they are actually valid.
void gl_SetupMenu()
{
#ifndef _MSC_VER
	FOptionValues **opt = OptionValues.CheckKey("HqResizeModes");
	if (opt != NULL) 
	{
		for(int i = (*opt)->mValues.Size()-1; i>=0; i--)
		{
			// Delete HQnX resize modes for non MSVC targets
			if ((*opt)->mValues[i].Value >= 7.0)
			{
				(*opt)->mValues.Delete(i);
			}
		}
	}
#endif
}
