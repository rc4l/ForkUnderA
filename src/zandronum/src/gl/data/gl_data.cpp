/*
** gl_data.cpp
** Maintenance data for GL renderer (mostly to handle rendering hacks)
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


// [rc4l] What GL alone keeps of a level: its wall cache and vertex buffer; the level's shared preparation is features/levelprep, MAPINFO's gl_renderer options features/rendersettings/mapoptions.cpp.

#include "gl/system/gl_system.h"
#include "r_state.h"

#include "gl/renderer/gl_renderer.h"
#include "gl/data/gl_data.h"
#include "gl/gl_functions.h"
#include "gl/scene/gl_wallcache.h"
#include "features/levelmesh/wallcache.h"   // [rc4l] RecalcVertexHeights

//==========================================================================
//
// Recalculate all heights affectting this vertex.
//
//==========================================================================
// [rc4l] Shared with the backend's bake, which splits its walls at the same heights (features/levelmesh, ComputeVertexHeights).
void gl_RecalcVertexHeights(vertex_t * v)
{
	zx::levelmesh::RecalcVertexHeights(v);
}

//==========================================================================
//
// GL's own data for a level the shared preparation has readied
//
//==========================================================================

void gl_PreprocessLevel()
{
	// [rc4l] GL's per-seg wall cache lives for the level.
	gl_WallCacheAlloc(numsegs);

	if (GLRenderer != NULL)
	{
		GLRenderer->SetupLevel();
	}
}

void gl_CleanLevelData()
{
	gl_WallCacheFree();
}
