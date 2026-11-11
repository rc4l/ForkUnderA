// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwshaders/hwshaders.h"
#include "features/hwshaders/computation/hwshader_compute.h"
#include "features/postprocess/postprocess.h"

#include "doomtype.h"
#include "zstring.h"
#include "tarray.h"
#include "name.h"
#include "sc_man.h"
#include "c_console.h"
#include "textures/textures.h"

#include <vector>

namespace zx { namespace hwshaders {

static std::vector<UserShaderKey> g_userShaders;

void ParseHardwareShader(FScanner &sc, int deflump)
{
	int type = FTexture::TEX_Any;
	FString maplumpname;
	float speed = 1.f;
	ProcessLighting lighting = kLightingZandronum;

	sc.MustGetString();
	// [Doruk-Aksoy] uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad: a post-process shader is a block of its own, not a texture's.
	if (sc.Compare("postprocess"))
	{
		zx::postprocess::ParsePostProcessShader(sc);
		return;
	}
	if (sc.Compare("texture")) type = FTexture::TEX_Wall;
	else if (sc.Compare("flat")) type = FTexture::TEX_Flat;
	else if (sc.Compare("sprite")) type = FTexture::TEX_Sprite;
	else sc.UnGet();

	sc.MustGetString();
	FTextureID no = TexMan.CheckForTexture(sc.String, type);
	FTexture *tex = TexMan[no];

	sc.MustGetToken('{');
	while (!sc.CheckToken('}'))
	{
		sc.MustGetString();
		if (sc.Compare("shader"))
		{
			sc.MustGetString();
			maplumpname = sc.String;
		}
		else if (sc.Compare("speed"))
		{
			sc.MustGetFloat();
			speed = float(sc.Float);
		}
		else if (sc.Compare("fua_ProcessLighting"))
		{
			sc.MustGetString();
			if (!ComputeProcessLightingOf(sc.String, lighting))
				sc.ScriptMessage("Unknown fua_ProcessLighting '%s', expected zandronum or uzdoom; keeping zandronum\n", sc.String);
		}
	}
	if (!tex || maplumpname.IsEmpty()) return;
	if (tex->bWarped != 0)
	{
		Printf("Cannot combine warping with hardware shader on texture '%s'\n", tex->Name.GetChars());
		return;
	}
	tex->gl_info.shaderspeed = speed;
	tex->gl_info.shaderindex = ComputeUserShaderSlot(g_userShaders, maplumpname.GetChars(), lighting) + kFirstUserShader;
}

int UserShaderCount() { return (int)g_userShaders.size(); }

const char *UserShaderLump(int i)
{
	return (i >= 0 && i < UserShaderCount()) ? g_userShaders[i].lump.c_str() : "";
}

ProcessLighting UserShaderLighting(int i)
{
	return (i >= 0 && i < UserShaderCount()) ? g_userShaders[i].lighting : kLightingZandronum;
}

int UserShaderOf(int shaderIndex)
{
	const int i = shaderIndex - kFirstUserShader;
	return (i >= 0 && i < UserShaderCount()) ? i : -1;
}

}} // namespace zx::hwshaders
