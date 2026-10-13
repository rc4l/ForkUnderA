// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwshaders/hwshaders.h"
#include "features/hwshaders/computation/hwshader_compute.h"

#include "doomtype.h"
#include "zstring.h"
#include "tarray.h"
#include "name.h"
#include "sc_man.h"
#include "c_console.h"
#include "textures/textures.h"

namespace zx { namespace hwshaders {

static TArray<FString> g_userShaders;

void ParseHardwareShader(FScanner &sc, int deflump)
{
	int type = FTexture::TEX_Any;
	FString maplumpname;
	float speed = 1.f;

	sc.MustGetString();
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
	}
	if (!tex || maplumpname.IsEmpty()) return;
	if (tex->bWarped != 0)
	{
		Printf("Cannot combine warping with hardware shader on texture '%s'\n", tex->Name.GetChars());
		return;
	}
	tex->gl_info.shaderspeed = speed;
	for (unsigned i = 0; i < g_userShaders.Size(); i++)
	{
		if (!g_userShaders[i].CompareNoCase(maplumpname))
		{
			tex->gl_info.shaderindex = i + kFirstUserShader;
			return;
		}
	}
	tex->gl_info.shaderindex = g_userShaders.Push(maplumpname) + kFirstUserShader;
}

int UserShaderCount() { return (int)g_userShaders.Size(); }

const char *UserShaderLump(int i)
{
	return (i >= 0 && (unsigned)i < g_userShaders.Size()) ? g_userShaders[i].GetChars() : "";
}

int UserShaderOf(int shaderIndex)
{
	const int i = shaderIndex - kFirstUserShader;
	return (i >= 0 && i < UserShaderCount()) ? i : -1;
}

}} // namespace zx::hwshaders
