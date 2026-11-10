// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2018 Kevin Caccamo
// Copyright 2018-2025 GZDoom Maintainers and Contributors
// Copyright 2025-2026 UZDoom Maintainers and Contributors
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Wavefront OBJ models, ported from uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad (src/common/models/models_obj.cpp): the parse and layout are computation/objmodel_compute, which drops what UZDoom read past its arrays.

#include "features/models/models.h"
#include "features/models/computation/modelskin_compute.h"   // [Doruk-Aksoy] the skin a surface draws with
#include "c_console.h"
#include "v_text.h"   // [Doruk-Aksoy] TEXTCOLOR_ORANGE
#include "w_wad.h"

#include <cstddef>
#include <string>
#include <vector>

// [Doruk-Aksoy] Ported from uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad: FOBJModel::Load, each `usemtl` loaded as a skin by its name, else beside the model; where UZDoom read a bad face's corners out of range, or lost the last faces of a file with faces before its first `usemtl`, here the parse drops the bad face, keeps the rest, and the load says so.
bool FOBJModel::Load(const char * path, int lumpnum, const char * buffer, int length)
{
	zx::models::ObjProblems problems;
	const bool any = zx::models::ComputeObjParse(buffer, length > 0 ? (size_t)length : 0, mData, problems);
	const char *name = Wads.GetLumpFullName(lumpnum);
	if (problems.badLines > 0 || problems.droppedFaces > 0 || problems.badUVs > 0)
		Printf(TEXTCOLOR_ORANGE "OBJ '%s': unreadable lines %d, faces left out %d (a corner past the vertices, or under three corners), texture coordinates past the file's %d\n",
			name, problems.badLines, problems.droppedFaces, problems.badUVs);
	if (!any)
	{
		Printf(TEXTCOLOR_ORANGE "OBJ '%s': no face to draw, so no model\n", name);
		return false;
	}
	if (mData.surfaces.size() > MD3_MAX_SURFACES)
		Printf(TEXTCOLOR_ORANGE "OBJ '%s': only the first %d of its %d materials are drawn\n", name, MD3_MAX_SURFACES, (int)mData.surfaces.size());
	mSkins.Clear();
	for (size_t i = 0; i < mData.surfaces.size(); i++)
	{
		const std::string &material = mData.surfaces[i].material;
		FTexture *skin = NULL;
		if (!material.empty())
		{
			skin = LoadSkin("", material.c_str());
			if (skin == NULL) skin = LoadSkin(path, material.c_str());
			if (skin == NULL) Printf(TEXTCOLOR_ORANGE "OBJ '%s': material '%s' (#%d) not found\n", name, material.c_str(), (int)i);
		}
		mSkins.Push(skin);
	}
	return true;
}

// [Doruk-Aksoy] An OBJ is one frame, whatever MODELDEF names.
int FOBJModel::FindFrame(const char *)
{
	return 0;
}

void FOBJModel::BuildGeometry()
{
	if (mGeometry.built) return;
	std::vector<zx::models::ObjDrawVertex> verts;
	std::vector<unsigned> first, count;
	zx::models::ComputeObjTriangles(mData, verts, first, count);
	mGeometry.verts.Resize((unsigned)verts.size());
	for (size_t i = 0; i < verts.size(); i++) mGeometry.verts[(unsigned)i].Set(verts[i].x, verts[i].y, verts[i].z, verts[i].u, verts[i].v);
	mSurfaceFirst.Clear();
	mSurfaceCount.Clear();
	for (size_t i = 0; i < first.size(); i++)
	{
		mSurfaceFirst.Push(first[i]);
		mSurfaceCount.Push(count[i]);
	}
	mGeometry.indexed = false;
	mGeometry.built = true;
}

// [Doruk-Aksoy] Ported from uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad: FOBJModel::RenderFrame, each surface with MODELDEF's Skin, its SurfaceSkin or its own, one with none left out; no more than MD3_MAX_SURFACES, the most a caller holds.
int FOBJModel::FrameSurfaces(const FSpriteModelFrame *smf, int slot, FTexture * skin, int frame, int frame2, FModelSurface * out)
{
	if (frame < 0 || frame2 < 0 || !mGeometry.built) return 0;
	int n = 0;
	for (unsigned i = 0; i < mSurfaceFirst.Size() && i < mSkins.Size() && i < MD3_MAX_SURFACES; i++)
	{
		FTexture *surfaceSkin = zx::models::ComputeModelSurfaceSkin(skin, smf->surfaceskins[slot][i], mSkins[i]);
		if (surfaceSkin == NULL) continue;
		out[n].skin = surfaceSkin;
		out[n].base1 = out[n].base2 = mSurfaceFirst[i];
		out[n].firstIndex = 0;
		out[n].count = mSurfaceCount[i];
		n++;
	}
	return n;
}
