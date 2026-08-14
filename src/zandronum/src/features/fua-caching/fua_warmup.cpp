//-----------------------------------------------------------------------------
//
// [ForkUnderA] fua-caching: GL pipeline warmup (see fua_caching.h).
//
// On GL-over-Metal (macOS) and some other drivers, the first draw with a new
// (shader program, blend state) combination triggers pipeline-state
// compilation -- hundreds of ms spread over the first frames that use them.
// In effect-heavy mods those combos first appear mid-fight (the first mass
// death), which shows up as hitch frames no texture precache can remove:
// pipelines are keyed on program+blend, not on textures.
//
// This pass runs once per session at level-load time (a GL context is
// current -- the material precache just uploaded textures) and draws one
// point per (program, blend triple) with the color mask off, forcing every
// pipeline the game can reach to compile up front. Programs are enumerated
// through FShaderManager::Get/BindEffect; blends are the realistic subset of
// what gl_GetRenderStyle can produce. ~140 draws, masked, then a glFinish.
//
//-----------------------------------------------------------------------------

#ifndef NO_GL

#include "gl/system/gl_system.h"
#include "doomtype.h"
#include "i_system.h"
#include "c_console.h"
#include "gl/system/gl_interface.h"
#include "gl/system/gl_cvars.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/shaders/gl_shader.h"
#include "gl/data/gl_vertexbuffer.h"
#include "features/fua-caching/fua_caching.h"

static void WarmupOnePoint ()
{
	FFlatVertex *ptr = GLRenderer->mVBO->GetBuffer();
	ptr->Set(0, 0, 0, 0, 0);
	ptr++;
	GLRenderer->mVBO->RenderCurrent(ptr, GL_POINTS);
}

void FUA_WarmupPipelines ()
{
	// Pipelines live for the whole process; one pass covers every later level.
	static bool done = false;
	if (done || GLRenderer == NULL || GLRenderer->mVBO == NULL || GLRenderer->mShaderManager == NULL)
		return;
	done = true;

	const unsigned int t0 = I_MSTime();

	// The realistic blend space out of gl_GetRenderStyle: normal translucency,
	// additive (both alpha- and color-weighted), opaque, shadow/fuzz's
	// DST_COLOR collapse, and the subtractive ops.
	static const struct { int src, dst, op; } blends[] =
	{
		{ GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_FUNC_ADD },
		{ GL_SRC_ALPHA, GL_ONE,                 GL_FUNC_ADD },
		{ GL_ONE,       GL_ONE,                 GL_FUNC_ADD },
		{ GL_ONE,       GL_ZERO,                GL_FUNC_ADD },
		{ GL_SRC_COLOR, GL_ONE,                 GL_FUNC_ADD },
		{ GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_FUNC_ADD },
		{ GL_SRC_ALPHA, GL_ONE,                 GL_FUNC_REVERSE_SUBTRACT },
		{ GL_ONE,       GL_ONE,                 GL_FUNC_REVERSE_SUBTRACT },
	};

	glColorMask(0, 0, 0, 0);

	int warmed = 0;
	for (unsigned int b = 0; b < countof(blends); ++b)
	{
		glBlendFunc(blends[b].src, blends[b].dst);
		glBlendEquation(blends[b].op);

		// Texture-effect programs, alpha-test variant (0.. until the manager runs out).
		for (unsigned int eff = 0; ; ++eff)
		{
			FShader *sh = GLRenderer->mShaderManager->Get(eff, true);
			if (sh == NULL)
				break;
			sh->Bind();
			WarmupOnePoint();
			++warmed;
		}
		// The no-alpha-test variants exist only for effects 0-3.
		for (unsigned int eff = 0; eff < 4; ++eff)
		{
			FShader *sh = GLRenderer->mShaderManager->Get(eff, false);
			if (sh == NULL)
				continue;
			sh->Bind();
			WarmupOnePoint();
			++warmed;
		}
		// Special-effect programs (fog boundary, spheremap, burn, stencil).
		for (int eff = 0; eff < MAX_EFFECTS; ++eff)
		{
			if (GLRenderer->mShaderManager->BindEffect(eff) != NULL)
			{
				WarmupOnePoint();
				++warmed;
			}
		}
	}

	glFinish(); // submit everything so compilation happens now, not at first present

	// Resync tracked state with what we changed behind FRenderState's back.
	glColorMask(1, 1, 1, 1);
	GLRenderer->mVBO->Reset();
	gl_RenderState.BlendEquation(GL_FUNC_ADD);
	gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl_RenderState.Apply();

	Printf("FUA caching: warmed %d pipeline states in %u ms\n", warmed, I_MSTime() - t0);
}

#else

void FUA_WarmupPipelines () {}

#endif // NO_GL
