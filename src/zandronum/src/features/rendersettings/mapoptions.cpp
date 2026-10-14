/*
** mapoptions.cpp
** MAPINFO's gl_renderer options
**
**---------------------------------------------------------------------------
** Copyright 2005 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
** 4. When not used as part of GZDoom or a GZDoom derivative, this code will be
**    covered by the terms of the GNU Lesser General Public License as published
**    by the Free Software Foundation; either version 2.1 of the License, or (at
**    your option) any later version.
** 5. Full disclosure of the entire project's source code, except for third
**    party libraries is mandatory. (NOTE: This clause is non-negotiable!)
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/


// [rc4l] MAPINFO's gl_renderer options and the settings they override, moved from gl/data/gl_data.cpp; both renderers read the result in glset.

#include "doomstat.h"   // [rc4l] zadmflags
#include "g_level.h"
#include "sc_man.h"
#include "c_dispatch.h"
#include "c_cvars.h"
#include "features/rendersettings/rendersettings.h"
#include "features/rendersettings/computation/mapoptions_compute.h"

EXTERN_CVAR(Int, r_lightmode)
EXTERN_CVAR(Bool, r_brightfog)

CUSTOM_CVAR(Bool, r_notexturefill, false, 0)
{
	glset.notexturefill = self;
}


CUSTOM_CVAR(Bool, r_nocoloredspritelighting, false, 0)
{
	glset.nocoloredspritelighting = self;
}

//==========================================================================
//
// MAPINFO stuff
//
//==========================================================================

struct FGLROptions : public FOptionalMapinfoData
{
	FGLROptions()
	{
		identifier = "gl_renderer";
		fogdensity = 0;
		outsidefogdensity = 0;
		skyfog = 0;
		// [rc4l] uzdoom@a8398e70b: without this the field holds whatever was on the heap, so the
		// MAPINFO brightfog option behaved randomly.
		brightfog = false;
		lightmode = -1;
		nocoloredspritelighting = -1;
		notexturefill = -1;
		skyrotatevector = FVector3(0,0,1);
		skyrotatevector2 = FVector3(0,0,1);
		pixelstretch = 1.2f;
	}
	virtual FOptionalMapinfoData *Clone() const
	{
		FGLROptions *newopt = new FGLROptions;
		newopt->identifier = identifier;
		newopt->fogdensity = fogdensity;
		newopt->outsidefogdensity = outsidefogdensity;
		newopt->skyfog = skyfog;
		newopt->lightmode = lightmode;
		newopt->nocoloredspritelighting = nocoloredspritelighting;
		newopt->notexturefill = notexturefill;
		newopt->skyrotatevector = skyrotatevector;
		newopt->skyrotatevector2 = skyrotatevector2;
		newopt->pixelstretch = pixelstretch;
		return newopt;
	}
	int			fogdensity;
	int			outsidefogdensity;
	int			skyfog;
	int			lightmode;
	int			brightfog;
	SBYTE		nocoloredspritelighting;
	SBYTE		notexturefill;
	FVector3	skyrotatevector;
	FVector3	skyrotatevector2;
	float		pixelstretch;
};

DEFINE_MAP_OPTION(fogdensity, false)
{
	FGLROptions *opt = info->GetOptData<FGLROptions>("gl_renderer");
	parse.ParseAssign();
	parse.sc.MustGetNumber();
	opt->fogdensity = parse.sc.Number;
}

DEFINE_MAP_OPTION(brightfog, false)
{
	FGLROptions *opt = info->GetOptData<FGLROptions>("gl_renderer");
	parse.ParseAssign();
	parse.sc.MustGetNumber();
	opt->brightfog = parse.sc.Number;
}

DEFINE_MAP_OPTION(outsidefogdensity, false)
{
	FGLROptions *opt = info->GetOptData<FGLROptions>("gl_renderer");
	parse.ParseAssign();
	parse.sc.MustGetNumber();
	opt->outsidefogdensity = parse.sc.Number;
}

DEFINE_MAP_OPTION(skyfog, false)
{
	FGLROptions *opt = info->GetOptData<FGLROptions>("gl_renderer");
	parse.ParseAssign();
	parse.sc.MustGetNumber();
	opt->skyfog = parse.sc.Number;
}

DEFINE_MAP_OPTION(lightmode, false)
{
	FGLROptions *opt = info->GetOptData<FGLROptions>("gl_renderer");
	parse.ParseAssign();
	parse.sc.MustGetNumber();
	opt->lightmode = BYTE(parse.sc.Number);
}

DEFINE_MAP_OPTION(nocoloredspritelighting, false)
{
	FGLROptions *opt = info->GetOptData<FGLROptions>("gl_renderer");
	if (parse.CheckAssign())
	{
		parse.sc.MustGetNumber();
		opt->nocoloredspritelighting = !!parse.sc.Number;
	}
	else
	{
		opt->nocoloredspritelighting = true;
	}
}

DEFINE_MAP_OPTION(notexturefill, false)
{
	FGLROptions *opt = info->GetOptData<FGLROptions>("gl_renderer");
	if (parse.CheckAssign())
	{
		parse.sc.MustGetNumber();
		opt->notexturefill = !!parse.sc.Number;
	}
	else
	{
		opt->notexturefill = true;
	}
}

DEFINE_MAP_OPTION(skyrotate, false)
{
	FGLROptions *opt = info->GetOptData<FGLROptions>("gl_renderer");

	parse.ParseAssign();
	parse.sc.MustGetFloat();
	opt->skyrotatevector.X = (float)parse.sc.Float;
	if (parse.format_type == FMapInfoParser::FMT_New) parse.sc.MustGetStringName(","); 
	parse.sc.MustGetFloat();
	opt->skyrotatevector.Y = (float)parse.sc.Float;
	if (parse.format_type == FMapInfoParser::FMT_New) parse.sc.MustGetStringName(","); 
	parse.sc.MustGetFloat();
	opt->skyrotatevector.Z = (float)parse.sc.Float;
	opt->skyrotatevector.MakeUnit();
}

DEFINE_MAP_OPTION(skyrotate2, false)
{
	FGLROptions *opt = info->GetOptData<FGLROptions>("gl_renderer");

	parse.ParseAssign();
	parse.sc.MustGetFloat();
	opt->skyrotatevector2.X = (float)parse.sc.Float;
	if (parse.format_type == FMapInfoParser::FMT_New) parse.sc.MustGetStringName(","); 
	parse.sc.MustGetFloat();
	opt->skyrotatevector2.Y = (float)parse.sc.Float;
	if (parse.format_type == FMapInfoParser::FMT_New) parse.sc.MustGetStringName(","); 
	parse.sc.MustGetFloat();
	opt->skyrotatevector2.Z = (float)parse.sc.Float;
	opt->skyrotatevector2.MakeUnit();
}

DEFINE_MAP_OPTION(pixelratio, false)
{
	FGLROptions *opt = info->GetOptData<FGLROptions>("gl_renderer");

	parse.ParseAssign();
	parse.sc.MustGetFloat();
	opt->pixelstretch = (float)parse.sc.Float;
}

bool IsLightmodeValid()
{
	return zx::rendersettings::ComputeLightmodeValid(glset.map_lightmode);
}

// [rc4l] The map's choices where it makes them, else the player's settings (the light mode's default while the server forces them).
static void ApplyMapSettings()
{
	const zx::rendersettings::MapRenderOptions map = { glset.map_lightmode, glset.map_brightfog, glset.map_nocoloredspritelighting, glset.map_notexturefill };
	const zx::rendersettings::PlayerRenderSettings player = { r_lightmode, r_lightmode.GetGenericRepDefault(CVAR_Int).Int,
		r_brightfog, r_nocoloredspritelighting, r_notexturefill, (zadmflags & ZADF_FORCE_VIDEO_DEFAULTS) != 0 };
	const zx::rendersettings::ResolvedRenderSettings r = zx::rendersettings::ComputeMapRenderSettings(map, player);
	glset.lightmode = r.lightmode;
	glset.brightfog = r.brightfog;
	glset.nocoloredspritelighting = r.nocoloredspritelighting;
	glset.notexturefill = r.notexturefill;
}

void InitGLRMapinfoData()
{
	FGLROptions *opt = level.info->GetOptData<FGLROptions>("gl_renderer", false);

	if (opt != NULL)
	{
		gl_SetFogParams(opt->fogdensity, level.info->outsidefog, opt->outsidefogdensity, opt->skyfog);
		glset.map_lightmode = opt->lightmode;
		glset.map_brightfog = opt->brightfog;
		glset.map_nocoloredspritelighting = opt->nocoloredspritelighting;
		glset.map_notexturefill = opt->notexturefill;
		glset.skyrotatevector = opt->skyrotatevector;
		glset.skyrotatevector2 = opt->skyrotatevector2;
		glset.pixelstretch = opt->pixelstretch;
	}
	else
	{
		gl_SetFogParams(0, level.info->outsidefog, 0, 0);
		glset.map_lightmode = -1;
		glset.map_brightfog = -1;
		glset.map_nocoloredspritelighting = -1;
		glset.map_notexturefill = -1;
		glset.skyrotatevector = FVector3(0,0,1);
		glset.skyrotatevector2 = FVector3(0,0,1);
		glset.pixelstretch = 1.2f;
	}
	ApplyMapSettings();
}

CCMD(gl_resetmap)
{
	ApplyMapSettings();
}
