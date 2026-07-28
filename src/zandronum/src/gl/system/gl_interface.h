#ifndef R_RENDER
#define R_RENDER

#include "basictypes.h"

enum RenderFlags
{
	// [BB] Added texture compression flags.
	RFL_TEXTURE_COMPRESSION=1,
	RFL_TEXTURE_COMPRESSION_S3TC=2,

	RFL_SHADER_STORAGE_BUFFER = 4,		// to be used later for a parameter buffer
	RFL_BUFFER_STORAGE = 8,				// allows persistently mapped buffers, which are the only efficient way to actually use a dynamic vertex buffer. If this isn't present, a workaround with uniform arrays is used.
	RFL_COREPROFILE = 16,
	RFL_NOBUFFER = 32,					// the static buffer makes no sense on GL 3.x AMD and Intel hardware, as long as compatibility mode is on

	// [rc4l] Retained past upstream 2925c96b5 for Zandronum-side call sites
	// ([BB] framebuffer/portal/trim checks, mcp_renderinfo): both are
	// guaranteed on every GL 3.x context, so they are set unconditionally.
	RFL_NPOT_TEXTURE = 64,
	RFL_OCCLUSION_QUERY = 128,
};

enum TexMode
{
	TM_MODULATE = 0,	// (r, g, b, a)
	TM_MASK,			// (1, 1, 1, a)
	TM_OPAQUE,			// (r, g, b, 1)
	TM_INVERSE,			// (1-r, 1-g, 1-b, a)
	TM_REDTOALPHA,		// (1, 1, 1, r)
	TM_CLAMPY,			// (r, g, b, (t >= 0.0 && t <= 1.0)? a:0)
};

struct RenderContext
{
	unsigned int flags;
	unsigned int maxuniforms;
	unsigned int maxuniformblock;
	unsigned int uniformblockalignment;
	float version;
	float glslversion;
	int max_texturesize;
	char * vendorstring;
	// [rc4l] Legacy field kept for the [BB] map_lightmode gates and
	// mcp_renderinfo; always 4 on the GL 3.x floor.
	unsigned int shadermodel;

	int MaxLights() const
	{
		return maxuniforms>=2048? 128:64;
	}
};

extern RenderContext gl;

#endif
