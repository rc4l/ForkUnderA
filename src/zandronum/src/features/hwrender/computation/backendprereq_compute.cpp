// [rc4l] See backendprereq_compute.h.
#include "backendprereq_compute.h"

const char *ComputeMissingPrereq(const BackendPrereqs &prereqs)
{
	if (!prereqs.frameBuffer)     return "zx_screen";
	if (!prereqs.lightBuffer)     return "zx_screen->mLights";
	if (!prereqs.boneBuffer)      return "zx_screen->mBones";
	if (!prereqs.viewpointBuffer) return "zx_screen->mViewpoints";
	if (!prereqs.vertexData)      return "zx_screen->mVertexData";
	if (!prereqs.renderer)        return "GLRenderer";
	if (!prereqs.samplerManager)  return "GLRenderer->mSamplerManager";
	if (!prereqs.shaderManager)   return "GLRenderer->mShaderManager";
	return nullptr;
}
