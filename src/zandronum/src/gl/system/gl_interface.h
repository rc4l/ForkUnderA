#ifndef R_RENDER
#define R_RENDER

#include "basictypes.h"

enum RenderFlags
{
	RFL_NPOT_TEXTURE=1,
	RFL_OCCLUSION_QUERY=4,
	// [BB] Added texture compression flags.
	RFL_TEXTURE_COMPRESSION=8,
	RFL_TEXTURE_COMPRESSION_S3TC=16,

	RFL_VBO = 32,
	RFL_MAP_BUFFER_RANGE = 64,
	RFL_FRAMEBUFFER = 128,
	RFL_TEXTUREBUFFER = 256,
	RFL_NVIDIA = 512,
	RFL_ATI = 1024,
	// [rc4l] Flight 2 (upstream 7d3beb665): persistent-mapped vertex buffers when available;
	// the vertex buffer falls back to glBufferData streaming without it (e.g. macOS).
	RFL_BUFFER_STORAGE = 2048,
	// [rc4l] Flight 3 (upstream 09f407143): dynamic-light capability check.
	RFL_SHADER_STORAGE_BUFFER = 4096,
	// [rc4l] Flight 12 (upstream ca76c2525): glDrawElementsBaseVertex for model buffers.
	RFL_BASEINDEX = 8192,


	RFL_GL_20 = 0x10000000,
	RFL_GL_21 = 0x20000000,
	RFL_GL_30 = 0x40000000,
	RFL_GL_40 = 0x80000000, // [BB]
};

enum TexMode
{
	TM_MODULATE = 0,	// (r, g, b, a)
	TM_MASK = 1,		// (1, 1, 1, a)
	TM_OPAQUE = 2,		// (r, g, b, 1)
	TM_INVERSE = 3,		// (1-r, 1-g, 1-b, a)
};

struct RenderContext
{
	float version;
	unsigned int flags;
	unsigned int shadermodel;
	unsigned int maxuniforms;
	int max_texturesize;
	char * vendorstring;
	// [rc4l] Flight 3 (upstream 09f407143): GLSL availability is a version check. On a macOS 2.1
	// compatibility context glslversion is 1.20, so hasGLSL() is false and the fixed-function path
	// runs until the core-profile flip restores shaders as GLSL 330.
	float glslversion;

	bool hasGLSL() const
	{
		return glslversion >= 1.3f;
	}

	int MaxLights() const
	{
		return maxuniforms>=2048? 128:64;
	}

	bool hasCompatibility()	// will return false, once transition to a core profile is possible and a core profile is used.
	{
		return true;
	}
};

extern RenderContext gl;

#endif

