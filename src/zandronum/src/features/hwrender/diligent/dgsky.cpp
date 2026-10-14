// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The sky: GL's dome or skybox, the skybox portal rendered from its viewpoint, the fade, and the sky surfaces and occluders cut from the world.

#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/viewsetup/viewsetup.h"   // [rc4l] the sky's scroll this frame
#include "features/hwrender/computation/skylook_compute.h"   // [rc4l] which sky a sector shows, and its dome's constants
#include "g_level.h"   // [rc4l] level.flags, the double and swapped skies
#include "r_sky.h"     // [rc4l] sky1texture and sky2texture

EXTERN_CVAR(Bool, fua_dg_sky)

namespace zx { namespace hwrender {

// ---------------------------------------------------------------------------
// Sky
// ---------------------------------------------------------------------------
//
// [rc4l] Without this the world sits under a flat void, which is most of what "it's not all of it"
// looks like: sky is not geometry in a Doom level, it is the *absence* of it. Sky walls and sky
// flats are handed to the portal manager and never reach the mesh, so wherever the map opens to sky
// the backend simply drew nothing.
//
// The dome is a transcription of FSkyVertexBuffer::SkyVertex (gl/scene/gl_skydome.cpp) -- same
// maxSideAngle, same 10000-unit radius, same mirrored X, same +300 nudge on the non-horizon rows,
// same UV convention. The per-texture-height model transform from RenderDome is baked into the
// vertices instead of being a matrix, since it only changes when the sky texture does.
//
// Drawn first with depth test and depth write OFF, so it fills the frame and the world paints over
// it. That is exactly Doom's rule -- sky is whatever is behind everything else -- and it means sky
// flats need no special handling at all: nothing is drawn there, so the sky survives.
// [rc4l] r,g,b,useTex carry the sky CAP.
//
// The dome only spans 60 degrees of elevation; above that there is a hole, and GZDoom fills it with
// a flat-coloured fan in the sky texture's average top colour (GetSkyCapColor). Without it the hole
// showed the backend's clear colour -- a dark navy disc straight overhead, which is what "the very
// top of the sky is missing the coloured thing GL has" was.
//
// useTex is 1 for the textured dome and 0 for the caps, so one pipeline draws both.
// [rc4l] alpha is separate from useTex because the dome's TOP ROW is translucent.
//
// GZDoom gives row 0 a vertex colour with zero alpha (SkyVertex: `r == 0 ? 0xffffff : 0xffffffff`),
// so the texture fades out toward the zenith and blends into the flat cap underneath. Without it the
// cap meets the dome at a hard circular seam -- a black disc stamped on the sky, which is what kept
// getting reported as missing fog.
struct SkyVertexData { float x, y, z, u, v, r, g, b, useTex, alpha; };

static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_skyPSO;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_skyFadePSO;
static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_skyFadeVB;
// [rc4l] A dome draw's own constants (ComputeSkyDrawConstants), written before each layer, since one frame can draw several skies.
static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_skyDrawCB;

// [rc4l] One sky texture's dome, built for its size and sky offset and bound to its material, as RenderDome sets its matrices per texture: the level's sky, a double sky's second layer, sky2 or a line's sky.
struct SkyDome
{
	int texture = -1;              // [rc4l] the FTextureID index it shows, -1 for an empty slot
	const void *material = nullptr;
	int builtWidth = -1, builtHeight = -1, skyOffset = 0;
	Diligent::RefCntAutoPtr<Diligent::IBuffer> vb;
	int verts = 0;
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
	PalEntry cap[2];               // [rc4l] [0] top cap, [1] bottom cap, GetSkyCapColor(false/true)
	float xscale = 4.f, vscale = 1.f;
	bool valid = false;
};
static const int kSkyDomes = 12;
static SkyDome g_domes[kSkyDomes];
// [rc4l] The level's own sky this frame (a sector with no sky of its own), which the background draws, and its two layers' domes.
static zx::hwrender::SkyLook g_defaultLook = { -1, 0.f, 0.f, false, -1, 0.f };
static SkyDome *g_defaultDome = NULL, *g_secondDome = NULL;
// [rc4l] The skybox's material the faces were built for, so an animation frame rebinds them only when it changes.
static const void *g_boxMaterial = NULL;

// [rc4l] A skybox is six textures on a cube, not one on a dome.
//
// GLDEFS resolves a map's sky name to an FSkyBox, which carries six FTextures and no pixels of its
// own. This backend detected one and drew it as an ordinary dome anyway, so a six-sided sky came out
// as one of its faces smeared around a cylinder -- it has been saying "[SKYBOX -- not implemented]"
// per sky since the sky path was written. See docs/skybox-scope.md.
//
// Each face is its own material, so each needs its own binding and its own draw, exactly as a world
// batch does. They share one vertex buffer; a face is a range in it.
struct SkyBoxFace
{
	int first, count;
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
};
static TArray<SkyBoxFace> g_skyBoxFaces;
static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_skyBoxVB;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_skyBoxPSO;
static bool g_skyIsBox = false;

// [rc4l] ...and a third kind of sky: one that is a PLACE.
//
// ZDoom's other skybox is a portal. An ASkyViewpoint actor sits in a room built to look like the
// horizon, and every sky surface shows the world as seen from it -- so the sky is a rendered scene,
// not a texture. Maps that use one set no sky texture at all, and this backend was falling back to
// the null placeholder and drawing a blue-and-white checkerboard. Proved by turning GL's own support
// off: `r_noskyboxes 1` makes GL draw exactly the checkerboard this did.
//
// Rendering it is the mirror pass again -- a second camera, an offscreen target, the same DrawWorld.
// What this backend can do that GL cannot is skip the stencils: sky is drawn FIRST here, full-screen
// and depth-off, with the world painted over it, and a skybox is at infinity, so its image simply IS
// that background.
static Diligent::RefCntAutoPtr<Diligent::ITexture>       g_skyPortalColor, g_skyPortalDepth;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_skyPortalPSO;
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_skyPortalSRB;
static int  g_skyPortalW = 0, g_skyPortalH = 0;
static bool g_skyPortalActive = false;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_skySurfPSO;
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_skySurfSRB;
static int g_skyDraws = 0;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_skySurfVB;
static unsigned int g_skySurfVBCapacity = 0;   // this level's sky is a portal

// [rc4l] The dome draw's constants: its turn in radians, u's sign, v's raise and the layer (ComputeSkyDrawConstants).
#define FUA_SKYDRAW_GLSL "layout(binding = 4) uniform SkyDraw { vec4 uSkyDraw; };\n"

static const char *kSkyVS =
	"#version 450\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aUV;\n"
	"layout(location = 2) in vec4 aSkyColor;\n"
	"layout(location = 3) in float aSkyAlpha;\n"
	FUA_CONSTANTS_GLSL
	FUA_SKYDRAW_GLSL
	"layout(location = 0) out vec2 vUV;\n"
	"layout(location = 1) out vec4 vSkyColor;\n"
	"layout(location = 2) out float vSkyAlpha;\n"
	"void main() {\n"
	// Centred on the camera, so the dome is effectively at infinity.
	//
	// [rc4l] The sky also SCROLLS. GLSkyInfo::x_offset carries mSky1Pos, which advances with
	// wall-clock time, and RenderDome folds it into the dome's Y rotation together with its constant
	// -180. Baking that into the vertices would mean rebuilding the buffer every frame, so it comes
	// in as an angle and is applied here.
	"    float sa = sin(uSkyDraw.x), ca = cos(uSkyDraw.x);\n"
	"    vec3 p = vec3(aPos.x * ca + aPos.z * sa, aPos.y, -aPos.x * sa + aPos.z * ca);\n"
	// [rc4l] Centred on the eye of whichever pass is drawing, read off its matrix as the horizon pass reads it, since uCameraPos holds only the player's and a skybox portal's sky sat around the player, thousands of units from its camera.
	"    vec4 e = inverse(uMVP) * vec4(0.0, 0.0, 1.0, 0.0);\n"
	"    gl_Position = uMVP * vec4(p + e.xyz / e.w, 1.0);\n"
	// [rc4l] RenderDome's texture matrix: u negated for a mirrored sky, v raised by the sky's y offset.
	"    vUV = vec2(aUV.x * uSkyDraw.y, aUV.y + uSkyDraw.z);\n"
	"    vSkyColor = aSkyColor;\n"
	"    vSkyAlpha = aSkyAlpha;\n"
	"}\n";

static const char *kSkyPS =
	"#version 450\n"
	FUA_CONSTANTS_GLSL
	FUA_FIXED_COLORMAP_GLSL
	FUA_SKYDRAW_GLSL
	"layout(location = 0) in vec2 vUV;\n"
	"layout(location = 1) in vec4 vSkyColor;\n"
	"layout(location = 2) in float vSkyAlpha;\n"
	"layout(binding = 1) uniform sampler2D uTex;\n"
	"layout(location = 0) out vec4 outColor;\n"
	// vSkyColor.a selects: 1 takes the texture (the dome), 0 takes the flat colour (the caps).
	"void main() {\n"
	// [rc4l] vSkyColor.a does three jobs, so the fade layer needs no extra vertex attribute:
	//   1  textured, opaque -- the dome
	//   0  flat colour, opaque -- the caps
	//  <0  flat colour, alpha = -a -- the sky fade layer GL draws over the whole sky
	"    float sel = max(vSkyColor.a, 0.0);\n"
	// [rc4l] A double sky's second layer (SKYMODE_SECONDLAYER): no caps, and the texture's own alpha over the first, below 0.05 dropped.
	"    if (uSkyDraw.w > 0.5) {\n"
	"        vec4 t = texture(uTex, vUV);\n"
	"        if (sel < 0.5 || t.a < 0.05) discard;\n"
	"        outColor = vec4(uFixedColormap.w > 0.5 ? fuaFixedColormap(t.rgb) : t.rgb, t.a * vSkyAlpha);\n"
	"        return;\n"
	"    }\n"
	"    float al = vSkyColor.a < 0.0 ? -vSkyColor.a : 1.0;\n"
	"    vec3 c = mix(vSkyColor.rgb, texture(uTex, vUV).rgb, sel);\n"
	// [rc4l] GL draws the dome and its caps through main.fp, so a fixed colormap recolours the sky too.
	"    if (uFixedColormap.w > 0.5) c = fuaFixedColormap(c);\n"
	"    outColor = vec4(c, al * vSkyAlpha);\n"
	"}\n";

// [rc4l] One sky dome vertex, with RenderDome's model and texture transforms folded in.
//
// modelScaleY/modelTransY are the model matrix (which moves the dome in world space) and vscale is
// the texture matrix's V scale. These are three separate things in the engine and were one here,
// which only works for the short skies Doom itself ships.
static void SkyVertexAt(int r, int c, int rows, int cols, bool yflip, float modelScaleY,
	float modelTransY, float xscale, float vscale, SkyVertexData &out)
{
	const float maxSideAngle = 60.0f * 3.14159265f / 180.0f;   // ANGLE_180/3
	const float scale = 10000.0f;

	const float topAngle  = (float)c / cols * 2.0f * 3.14159265f;
	const float sideAngle = maxSideAngle * (float)(rows - r) / rows;
	const float height    = sinf(sideAngle);
	const float realRadius = scale * cosf(sideAngle);

	float x = realRadius * cosf(topAngle);
	float y = yflip ? -(scale * height) : (scale * height);
	float z = realRadius * sinf(topAngle);
	if (r != rows) y += 300.0f;

	// [rc4l] Only the vertex negation, NOT RenderDome's additional -180 degree Y rotation.
	//
	// Composing both (which leaves x alone and negates z) was tried and measured worse -- 18.5 against
	// 16.7 on gvh04 -- so the rotation is evidently already accounted for by the view basis this
	// backend uses. Recorded because it is the obvious "fix" to try, and it is wrong.
	out.x = -x;
	out.y = (y - 1.0f) * modelScaleY + modelTransY;   // RenderDome's scale-then-translate
	out.z = z;
	out.u = (-(float)c / cols) * xscale;
	out.v = (yflip ? (1.0f + (float)(rows - r) / rows) : ((float)r / rows)) * vscale;
}

// Radians, recomputed per frame: -180 degrees (RenderDome's constant) plus the level sky's scroll, which the scene constants carry for a traced ray that misses.
static float g_skyAngle = 0.f;

static void BuildSkyDome(SkyDome &d, int texw, int texh)
{
	const int rows = 4, cols = 128;

	// [rc4l] RenderDome's four height cases, in full.
	//
	// Only the short-sky branch was implemented, and the -1250 world translate that goes with it was
	// applied unconditionally. Doom's own skies are 256x128 so they land in that branch and looked
	// right, which is exactly why this survived: every stock map agreed with GL. A mod with a TALL
	// sky -- Ghouls vs Humans among them -- takes one of the other two branches, where both the scale
	// and the translate are different, and its sky came out showing the wrong band of the texture.
	//
	// A tall sky also needs the texture matrix's V scale, which is a separate thing from the model
	// matrix's Y scale and was not represented here at all.
	const float skyoffsetfactor = 57.f;
	float modelScaleY = 1.f, modelTransY = 0.f, vscale = 1.f;
	if (texh < 128)
	{
		modelTransY = -1250.f; modelScaleY = 128 / 230.f;
		vscale = (texh > 0) ? (float)(128 / texh) : 1.f;   // integer division, as upstream
	}
	else if (texh < 200)
	{
		modelTransY = -1250.f; modelScaleY = texh / 230.f;
	}
	else if (texh <= 240)
	{
		modelTransY = (200 - texh + d.skyOffset + skyoffset) * skyoffsetfactor;
		modelScaleY = 1.f + ((texh - 200.f) / 200.f) * 1.17f;
	}
	else
	{
		modelTransY = (-40 + d.skyOffset + skyoffset) * skyoffsetfactor;
		modelScaleY = 1.2f * 1.17f;
		vscale = (texh > 0) ? 240.f / texh : 1.f;
	}
	const float xscale = texw > 0 ? 1024.f / texw : 1.f;
	d.xscale = xscale; d.vscale = vscale;

	TArray<SkyVertexData> verts;
	for (int hemi = 0; hemi < 2; hemi++)
	{
		const bool yflip = (hemi == 1);

		// [rc4l] Cap the hole the dome leaves -- BEFORE the strips, which is what RenderDome does.
		//
		// The dome only reaches 60 degrees of elevation, so straight up there is nothing, and what
		// showed through was the backend's clear colour: a dark navy disc overhead on every open map.
		// GZDoom fills it with a flat fan in the sky texture's own average edge colour.
		//
		// The order is not incidental. The cap is a flat lid at row 1's height, so from the centre it
		// subtends everything from 45 degrees up -- it overlaps the textured 45-to-60 band rather than
		// merely abutting it. Drawn afterwards, with no depth test on the sky, it painted flat colour
		// over a third of the visible sky: a huge black disc where GL has cloud. Drawn first, the
		// strips cover it back up and only the part above the dome remains.
		{
			const PalEntry cap = d.cap[hemi ? 1 : 0];
			SkyVertexData centre;
			SkyVertexAt(1, 0, rows, cols, yflip, modelScaleY, modelTransY, xscale, vscale, centre);
			for (int c = 1; c < cols; c++)
			{
				SkyVertexData a, b;
				SkyVertexAt(1, c,     rows, cols, yflip, modelScaleY, modelTransY, xscale, vscale, a);
				SkyVertexAt(1, c + 1, rows, cols, yflip, modelScaleY, modelTransY, xscale, vscale, b);
				SkyVertexData tri[3] = { centre, a, b };
				for (int k = 0; k < 3; k++)
				{
					tri[k].r = cap.r / 255.f; tri[k].g = cap.g / 255.f; tri[k].b = cap.b / 255.f;
					tri[k].useTex = 0.f;
					tri[k].alpha = 1.f;
					tri[k].y = centre.y;   // a lid, not part of the dome's curve
				}
				verts.Push(tri[0]); verts.Push(tri[1]); verts.Push(tri[2]);
			}
		}

		for (int r = 0; r < rows; r++)
		{
			for (int c = 0; c < cols; c++)
			{
				SkyVertexData q[4];
				SkyVertexAt(r,     c,     rows, cols, yflip, modelScaleY, modelTransY, xscale, vscale, q[0]);
				SkyVertexAt(r + 1, c,     rows, cols, yflip, modelScaleY, modelTransY, xscale, vscale, q[1]);
				SkyVertexAt(r,     c + 1, rows, cols, yflip, modelScaleY, modelTransY, xscale, vscale, q[2]);
				SkyVertexAt(r + 1, c + 1, rows, cols, yflip, modelScaleY, modelTransY, xscale, vscale, q[3]);
				// Row 0 is the zenith edge and fades out, exactly as SkyVertex's vertex colour does.
				for (int k = 0; k < 4; k++) { q[k].r = q[k].g = q[k].b = 1.f; q[k].useTex = 1.f; }
				q[0].alpha = q[2].alpha = (r == 0) ? 0.f : 1.f;
				q[1].alpha = q[3].alpha = 1.f;
				verts.Push(q[0]); verts.Push(q[1]); verts.Push(q[2]);
				verts.Push(q[2]); verts.Push(q[1]); verts.Push(q[3]);
			}
		}
	}

	d.verts = (int)verts.Size();
	d.vb.Release();
	if (d.verts == 0) return;

	Diligent::BufferDesc bd;
	bd.Name = "fua sky VB";
	bd.Size = (Diligent::Uint64)d.verts * sizeof(SkyVertexData);
	bd.Usage = Diligent::USAGE_IMMUTABLE;
	bd.BindFlags = Diligent::BIND_VERTEX_BUFFER;
	Diligent::BufferData bdata;
	bdata.pData = &verts[0];
	bdata.DataSize = bd.Size;
	GetDevice()->CreateBuffer(bd, &bdata, &d.vb);
}

// [rc4l] One creator, two pipelines. The dome WRAPS horizontally -- a cylinder has to, the texture
// meets itself -- and a box face must CLAMP both ways or its edge pixels bleed across the seam,
// which is what GL asks for with CLAMP_XY. Everything else about the two is identical.
static bool CreateSkyPipeline(Diligent::TEXTURE_ADDRESS_MODE addrU, const char *name,
	Diligent::RefCntAutoPtr<Diligent::IPipelineState> &outPSO)
{
	if (outPSO) return true;
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;

	Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
	vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua sky VS", kSkyVS);
	ps = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua sky PS", kSkyPS);
	if (!vs || !ps) return false;

	Diligent::LayoutElement layout[] = {
		Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{2, 0, 4, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{3, 0, 1, Diligent::VT_FLOAT32, false},
	};
	static Diligent::ShaderResourceVariableDesc vars[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	// [rc4l] Not static: a static array is filled on the first call only, so the skybox's clamp took the dome's wrap and blended each face's edge with its opposite one.
	Diligent::SamplerDesc samp;
	// [rc4l] The engine's filter, as GL binds a sky texture with, where a fixed linear one smoothed every sky GL draws crisp.
	FillSamplerFromEngine(samp);
	samp.AddressU = addrU;
	samp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;   // no wrap top-to-bottom
	const Diligent::ImmutableSamplerDesc samplers[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", samp },
		{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", samp },
	};

	Diligent::GraphicsPipelineStateCreateInfo pci;
	pci.PSODesc.Name = name;
	pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
	pci.GraphicsPipeline.NumRenderTargets = 1;
	pci.GraphicsPipeline.RTVFormats[0] = swap->GetDesc().ColorBufferFormat;
	pci.GraphicsPipeline.DSVFormat = swap->GetDesc().DepthBufferFormat;
	pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
	// The sky is behind everything: never tested, never written.
	pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
	pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
	// Blended, so the dome's fading top row composites over the cap drawn beneath it.
	{
		auto &rt = pci.GraphicsPipeline.BlendDesc.RenderTargets[0];
		rt.BlendEnable = true;
		rt.SrcBlend  = Diligent::BLEND_FACTOR_SRC_ALPHA;
		rt.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
		rt.BlendOp = Diligent::BLEND_OPERATION_ADD;
		rt.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
		rt.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
		rt.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
	}
	pci.GraphicsPipeline.InputLayout.LayoutElements = layout;
	pci.GraphicsPipeline.InputLayout.NumElements = 4;
	pci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
	pci.PSODesc.ResourceLayout.Variables = vars;
	pci.PSODesc.ResourceLayout.NumVariables = 1;
	pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
	pci.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
	pci.pVS = vs;
	pci.pPS = ps;
	if (!g_skyDrawCB)
	{
		Diligent::BufferDesc cbd;
		cbd.Name = "fua sky draw constants";
		cbd.Size = sizeof(float) * 4;
		cbd.Usage = Diligent::USAGE_DYNAMIC;
		cbd.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
		cbd.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
		dev->CreateBuffer(cbd, nullptr, &g_skyDrawCB);
		if (!g_skyDrawCB) return false;
	}
	dev->CreateGraphicsPipelineState(pci, &outPSO);
	if (!outPSO) return false;
	BindSceneConstants(outPSO);
	for (Diligent::SHADER_TYPE stage : { Diligent::SHADER_TYPE_VERTEX, Diligent::SHADER_TYPE_PIXEL })
		if (auto *v = outPSO->GetStaticVariableByName(stage, "SkyDraw")) v->Set(g_skyDrawCB);
	return true;
}

static bool EnsureSkyPipeline()
{
	return CreateSkyPipeline(Diligent::TEXTURE_ADDRESS_WRAP, "fua sky PSO", g_skyPSO);
}

// [rc4l] The next dome draw's constants, for its sky's scroll, mirror and raise and its layer.
static void SetSkyDraw(Diligent::IDeviceContext *ctx, float xOffset, bool mirrored, float yOffset, const SkyDome *d, bool secondLayer)
{
	float c[4];
	zx::hwrender::ComputeSkyDrawConstants(xOffset, mirrored, yOffset, d != NULL ? d->vscale : 1.f, d != NULL ? d->builtHeight : 0, secondLayer, c);
	Diligent::MapHelper<float> cb(ctx, g_skyDrawCB, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
	for (int i = 0; i < 4; i++) cb[i] = c[i];
}

// [rc4l] Whether texture id draws, as GLSkyInfo's ValidateTexture and its TEX_Null test ask of a transfer line's.
static bool SkyTextureDraws(FTextureID id)
{
	FTexture *tex = id.isValid() ? TexMan[id] : NULL;
	return tex != NULL && tex->UseType != FTexture::TEX_Null;
}

// [rc4l] The sky a sector whose sky field is sectorSky shows this frame, as GLWall::SkyPlane reads the level, the transfer line and the scroll.
static zx::hwrender::SkyLook LookOfSky(int sectorSky)
{
	zx::hwrender::SkyLookIn in = {};
	in.sectorSky = sectorSky;
	const int line = (sectorSky & (zx::hwrender::kSkyFlatFlag - 1)) - 1;
	if ((sectorSky & zx::hwrender::kSkyFlatFlag) && line >= 0 && line < numlines && lines[line].sidedef[0] != NULL)
	{
		const side_t *s = lines[line].sidedef[0];
		const FTextureID top = s->GetTexture(side_t::top), bottom = s->GetTexture(side_t::bottom);
		in.lineTop = top.GetIndex(); in.lineBottom = bottom.GetIndex();
		in.lineTopDraws = SkyTextureDraws(top);
		in.lineBottomValid = bottom.isValid();
		in.lineBottomDraws = SkyTextureDraws(bottom);
		// [rc4l] ANGLE_TO_FLOAT: the x offset read as an angle, in degrees.
		in.lineTopX = (float)(s->GetTextureXOffset(side_t::top).Raw() * 180. / ANGLE_180);
		in.lineBottomX = (float)(s->GetTextureXOffset(side_t::bottom).Raw() * 180. / ANGLE_180);
		in.lineTopY = FIXED2FLOAT(s->GetTextureYOffset(side_t::top));
		in.lineBottomY = FIXED2FLOAT(s->GetTextureYOffset(side_t::bottom));
		in.lineMirrored = !lines[line].args[2];
	}
	in.swapSkies = (level.flags & LEVEL_SWAPSKIES) != 0;
	in.doubleSky = (level.flags & LEVEL_DOUBLESKY) != 0;
	in.sky1 = sky1texture.GetIndex();
	in.sky2 = sky2texture.GetIndex();
	in.sky1Pos = zx::viewsetup::CurrentView().sky1Pos;
	in.sky2Pos = zx::viewsetup::CurrentView().sky2Pos;
	return zx::hwrender::ComputeSkyLook(in);
}

// [rc4l] Fills d for texture index `texture` as it shows now (an animated sky's frame), rebuilding the dome only when its size or sky offset changed and the binding when its frame did; false when it has no material.
static bool PrepareDome(SkyDome &d, int texture)
{
	FTexture *raw = texture >= 0 ? TexMan.ByIndexTranslated(texture) : NULL;
	const void *mat = zx::materials::MaterialForTexture(raw);
	zx::materials::MaterialInfo info;
	if (raw == NULL || !zx::materials::DescribeMaterial(mat, info)) { d.valid = false; return false; }
	if (d.valid && d.texture == texture && d.material == mat) return true;
	const bool sameShape = d.valid && d.builtWidth == info.renderWidth && d.builtHeight == info.renderHeight && d.skyOffset == raw->SkyOffset;
	d.texture = texture;
	d.skyOffset = raw->SkyOffset;
	d.cap[0] = raw->GetSkyCapColor(false);
	d.cap[1] = raw->GetSkyCapColor(true);
	if (!sameShape)
	{
		// [rc4l] Said once per genuinely new texture rather than per animation frame, which is what identified a frozen animation before.
		Printf("vulkan sky: %s %dx%d skyoffset %d  cap %d,%d,%d / %d,%d,%d\n", info.name, info.renderWidth, info.renderHeight, d.skyOffset,
			d.cap[0].r, d.cap[0].g, d.cap[0].b, d.cap[1].r, d.cap[1].g, d.cap[1].b);
		BuildSkyDome(d, info.renderWidth, info.renderHeight);
		d.builtWidth = info.renderWidth;
		d.builtHeight = info.renderHeight;
	}
	if (!d.srb) g_skyPSO->CreateShaderResourceBinding(&d.srb, true);
	if (!d.srb) { d.valid = false; return false; }
	if (auto *v = d.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex")) v->Set(GetMaterialSRV(mat, 0));
	d.material = mat;
	d.valid = d.verts > 0;
	return d.valid;
}

// [rc4l] The dome showing texture index `texture`, from the slot already holding it or else a free one (the last slot taken over when none is), or NULL when it cannot be drawn.
static SkyDome *DomeFor(int texture)
{
	if (texture < 0 || !EnsureSkyPipeline()) return NULL;
	SkyDome *slot = NULL;
	for (int i = 0; i < kSkyDomes && slot == NULL; i++) if (g_domes[i].texture == texture) slot = &g_domes[i];
	for (int i = 0; i < kSkyDomes && slot == NULL; i++) if (g_domes[i].texture < 0) slot = &g_domes[i];
	if (slot == NULL) { slot = &g_domes[kSkyDomes - 1]; slot->valid = false; }
	return PrepareDome(*slot, texture) ? slot : NULL;
}

// [rc4l] One layer of a sky: RenderDome with its scroll, mirror and raise, the second layer of a double sky without its caps.
static void DrawDome(Diligent::IDeviceContext *ctx, const SkyDome &d, float xOffset, bool mirrored, float yOffset, bool secondLayer)
{
	if (!d.valid || !d.vb || !d.srb) return;
	SetSkyDraw(ctx, xOffset, mirrored, yOffset, &d, secondLayer);
	Diligent::IBuffer *vbs[] = { d.vb };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	ctx->SetPipelineState(g_skyPSO);
	ctx->CommitShaderResources(d.srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	Diligent::DrawAttribs draw;
	draw.NumVertices = (Diligent::Uint32)d.verts;
	draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
	ctx->Draw(draw);
}

static void DrawSkyFade(Diligent::IDeviceContext *ctx);

// [rc4l] A sky look as GLSkyPortal::DrawContents draws it: the main layer, a double sky's second over it, and the fade.
static void DrawLook(Diligent::IDeviceContext *ctx, const zx::hwrender::SkyLook &look, const SkyDome *main, const SkyDome *second)
{
	if (main != NULL) DrawDome(ctx, *main, look.xOffset, look.mirrored, look.yOffset, false);
	if (second != NULL) DrawDome(ctx, *second, look.secondXOffset, false, look.yOffset, true);
	DrawSkyFade(ctx);
}

// The box gets its own pipeline for the sampler, and a binding PER FACE rather than one shared.
// [rc4l] The cube, transcribed from GL's RenderBox.
//
// Coordinates are GL's own: +/-128 with Y up, which is the space the sky vertex shader already works
// in -- it adds uCameraPos and rotates (x, z) about Y by the same (-180 + scroll) angle GL puts on
// its model matrix. So the box needs no new transform and no shader change; useTex 1 makes the pixel
// shader take the texture, which is what a face is.
//
// GL draws each face as a four-vertex strip and this pipeline is a triangle list, so each becomes
// (0,1,2) and (2,1,3). Winding does not matter: the sky pipeline culls nothing.
static void PushSkyQuad(TArray<SkyVertexData> &v,
	const float px[4], const float py[4], const float pz[4], const float uu[4], const float vv[4])
{
	static const int order[6] = { 0, 1, 2, 2, 1, 3 };
	for (int i = 0; i < 6; i++)
	{
		const int k = order[i];
		SkyVertexData d;
		d.x = px[k]; d.y = py[k]; d.z = pz[k];
		d.u = uu[k]; d.v = vv[k];
		d.r = d.g = d.b = 1.f;
		d.useTex = 1.f;    // textured and opaque -- see kSkyPS
		d.alpha  = 1.f;
		v.Push(d);
	}
}

static void BuildSkyBox(const zx::materials::SkyBoxFaces &box)
{
	g_skyBoxFaces.Clear();
	g_skyBoxVB.Release();
	const zx::materials::SkyBoxFaces *sb = &box;

	TArray<SkyVertexData> verts;
	struct Pending { int first, count; FTexture *tex; };
	TArray<Pending> pending;

	// [rc4l] Two forms, told apart exactly as RenderBox tells them apart.
	//
	// With six faces each wall is its own texture. With three, faces[0] is ONE texture wrapped right
	// around the four walls -- its U runs 0, .25, .5, .75, 1 -- and the top and bottom move down to
	// faces[1] and faces[2]. Getting this wrong shows up as three of the four walls being the same
	// picture, which looks plausible enough to miss.
	const bool sixFace = (sb->faces[5] != NULL);
	const int  topIdx  = sixFace ? 4 : 1;
	const int  botIdx  = sixFace ? 5 : 2;

	const float T = 128.f;
	if (sixFace)
	{
		//                      north              east               south              west
		const float qx[4][4] = {{ T,-T, T,-T}, {-T,-T,-T,-T}, {-T, T,-T, T}, { T, T, T, T}};
		const float qy[4][4] = {{ T, T,-T,-T}, { T, T,-T,-T}, { T, T,-T,-T}, { T, T,-T,-T}};
		const float qz[4][4] = {{-T,-T,-T,-T}, {-T, T,-T, T}, { T, T, T, T}, { T,-T, T,-T}};
		const float qu[4]    = { 0.f, 1.f, 0.f, 1.f };
		const float qv[4]    = { 0.f, 0.f, 1.f, 1.f };
		for (int f = 0; f < 4; f++)
		{
			const int first = (int)verts.Size();
			PushSkyQuad(verts, qx[f], qy[f], qz[f], qu, qv);
			Pending pd; pd.first = first; pd.count = (int)verts.Size() - first; pd.tex = sb->faces[f];
			pending.Push(pd);
		}
	}
	else
	{
		// One texture around all four walls: four quads sharing a material, so ONE draw.
		const float wx[4][4] = {{ T,-T, T,-T}, {-T,-T,-T,-T}, {-T, T,-T, T}, { T, T, T, T}};
		const float wy[4]    = { T, T,-T,-T };
		const float wz[4][4] = {{-T,-T,-T,-T}, {-T, T,-T, T}, { T, T, T, T}, { T,-T, T,-T}};
		const int first = (int)verts.Size();
		for (int f = 0; f < 4; f++)
		{
			const float u0 = f * 0.25f, u1 = u0 + 0.25f;
			const float uu[4] = { u0, u1, u0, u1 };
			const float vv[4] = { 0.f, 0.f, 1.f, 1.f };
			PushSkyQuad(verts, wx[f], wy, wz[f], uu, vv);
		}
		Pending pd; pd.first = first; pd.count = (int)verts.Size() - first; pd.tex = sb->faces[0];
		pending.Push(pd);
	}

	// Top. fliptop swaps its V, which is how a skybox author turns the ceiling the right way up.
	{
		const float tx[4] = { T, -T,  T, -T };
		const float ty[4] = { T,  T,  T,  T };
		const float tz[4] = { -T, -T, T,  T };
		const float ftop  = sb->flipTop ? 1.f : 0.f;
		const float tu[4] = { 0.f, 1.f, 0.f, 1.f };
		const float tv[4] = { ftop, ftop, 1.f - ftop, 1.f - ftop };
		const int first = (int)verts.Size();
		PushSkyQuad(verts, tx, ty, tz, tu, tv);
		Pending pd; pd.first = first; pd.count = (int)verts.Size() - first; pd.tex = sb->faces[topIdx];
		pending.Push(pd);
	}
	// Bottom.
	{
		const float bx[4] = { T, -T,  T, -T };
		const float by[4] = { -T, -T, -T, -T };
		const float bz[4] = { -T, -T,  T,  T };
		const float bu[4] = { 0.f, 1.f, 0.f, 1.f };
		const float bv[4] = { 0.f, 0.f, 1.f, 1.f };
		const int first = (int)verts.Size();
		PushSkyQuad(verts, bx, by, bz, bu, bv);
		Pending pd; pd.first = first; pd.count = (int)verts.Size() - first; pd.tex = sb->faces[botIdx];
		pending.Push(pd);
	}

	if (verts.Size() == 0) return;

	Diligent::BufferDesc bd;
	bd.Name = "fua skybox VB";
	bd.Size = (Diligent::Uint64)verts.Size() * sizeof(SkyVertexData);
	bd.Usage = Diligent::USAGE_IMMUTABLE;
	bd.BindFlags = Diligent::BIND_VERTEX_BUFFER;
	Diligent::BufferData bdata;
	bdata.pData = &verts[0];
	bdata.DataSize = bd.Size;
	GetDevice()->CreateBuffer(bd, &bdata, &g_skyBoxVB);
	if (!g_skyBoxVB) return;

	// A binding per face, from the same material path the world uses -- a face that cannot be
	// resolved falls back to white rather than dropping the face and leaving a hole.
	for (unsigned i = 0; i < pending.Size(); i++)
	{
		SkyBoxFace face;
		face.first = pending[i].first;
		face.count = pending[i].count;
		g_skyBoxPSO->CreateShaderResourceBinding(&face.srb, true);
		if (!face.srb) continue;
		const void *fm = zx::materials::MaterialForTexture(pending[i].tex);
		if (auto *v = face.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex"))
			v->Set(GetMaterialSRV(fm, 0));
		g_skyBoxFaces.Push(face);
	}
}

static bool EnsureSkyBoxPipeline()
{
	return CreateSkyPipeline(Diligent::TEXTURE_ADDRESS_CLAMP, "fua skybox PSO", g_skyBoxPSO);
}

// Rebuild the dome and its binding when the level's sky texture changes.
void EnsureSky()
{
	if (!EnsureSkyPipeline()) return;
	// [rc4l] The level's sky for a sector with none of its own: sky1, or sky2 for a double or swapped sky, under sky1 as a double sky's second layer.
	g_defaultLook = LookOfSky(0);
	g_defaultDome = g_secondDome = NULL;
	// [rc4l] Translated, not as authored: GvH's skies are animated sequences, and the subscript pinned frame one forever while GL cycled.
	FTexture *raw = g_defaultLook.texture >= 0 ? TexMan.ByIndexTranslated(g_defaultLook.texture) : NULL;
	const void *mat = zx::materials::MaterialForTexture(raw);
	zx::materials::MaterialInfo info;
	if (raw == NULL || !zx::materials::DescribeMaterial(mat, info)) { g_skyIsBox = false; return; }
	// [rc4l] Say what the sky actually IS, once per genuinely new texture rather than per animation
	// frame. Comparing this line against the engine's own is what identified both the skybox that is
	// unimplemented and the animation that was frozen.
	// [rc4l] A skybox is a CUBE, and until now this drew it as a dome anyway.
	//
	// GLDEFS resolves the map's sky to an FSkyBox, which is six textures and no pixels of its own --
	// so the dome sampled one face and wrapped it around a cylinder. The marker that used to print
	// here said as much on every such sky.
	zx::materials::SkyBoxFaces box;
	const bool isBox = zx::materials::SkyBoxFacesOf(raw, box);
	if (isBox)
	{
		const zx::materials::SkyBoxFaces *sb = &box;
		if (g_skyIsBox && g_boxMaterial == mat && g_skyBoxFaces.Size() > 0) return;
		const bool sameBox = g_skyIsBox && g_boxMaterial == mat;
		if (!EnsureSkyBoxPipeline()) { g_skyIsBox = false; return; }
		BuildSkyBox(box);
		if (g_skyBoxFaces.Size() == 0) { g_skyIsBox = false; return; }
		if (!sameBox)
		{
			Printf("vulkan sky: %s  [SKYBOX, %s: %s %s %s %s %s %s]\n",
				info.name,
				(sb->faces[5] != NULL) ? "six faces" : "wrapped",
				(sb->faces[0] && sb->faces[0]->Name) ? sb->faces[0]->Name : "-",
				(sb->faces[1] && sb->faces[1]->Name) ? sb->faces[1]->Name : "-",
				(sb->faces[2] && sb->faces[2]->Name) ? sb->faces[2]->Name : "-",
				(sb->faces[3] && sb->faces[3]->Name) ? sb->faces[3]->Name : "-",
				(sb->faces[4] && sb->faces[4]->Name) ? sb->faces[4]->Name : "-",
				(sb->faces[5] && sb->faces[5]->Name) ? sb->faces[5]->Name : "-");
		}
		g_skyIsBox = true;
		g_boxMaterial = mat;
		return;
	}

	g_skyIsBox = false;
	g_boxMaterial = NULL;
	g_defaultDome = DomeFor(g_defaultLook.texture);
	g_secondDome = g_defaultLook.secondTexture >= 0 ? DomeFor(g_defaultLook.secondTexture) : NULL;
}

// [rc4l] The sky's fade layer: a translucent sheet in the sector's fade colour, over the sky and
// under the world.
//
// GL draws this and this backend did not, so a foggy map's sky came out as a black hole where GL had
// haze -- reported as "fog missing".
//
// It gets its own shader pair rather than riding the dome's. GL's version is four triangles a single
// unit across, drawn with depth clamping so that a shape entirely inside the near plane still covers
// the screen. Reproducing that by placing world-space geometry is fighting the projection: the first
// attempt put a horizontal sheet 60000 units overhead, which is a ceiling rather than something that
// encloses the camera, and covered nothing but the top of the frame. A quad written straight into
// clip space is what the thing actually is.
static const char *kSkyFadeVS =
	"#version 450\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aUV;\n"
	"layout(location = 2) in vec4 aSkyColor;\n"
	"layout(location = 0) out vec4 vFade;\n"
	"void main() {\n"
	"    vFade = vec4(aSkyColor.rgb, -aSkyColor.a);\n"
	"    gl_Position = vec4(aPos.xy, 0.0, 1.0);\n"
	"}\n";

static const char *kSkyFadePS =
	"#version 450\n"
	"layout(location = 0) in vec4 vFade;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() { outColor = vFade; }\n";

static bool EnsureSkyFadePipeline()
{
	if (g_skyFadePSO) return true;
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;

	Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
	vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua sky fade VS", kSkyFadeVS);
	ps = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua sky fade PS", kSkyFadePS);
	if (!vs || !ps) return false;

	Diligent::LayoutElement layout[] = {
		Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{2, 0, 4, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{3, 0, 1, Diligent::VT_FLOAT32, false},
	};
	Diligent::GraphicsPipelineStateCreateInfo pci;
	pci.PSODesc.Name = "fua sky fade PSO";
	pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
	pci.GraphicsPipeline.NumRenderTargets = 1;
	pci.GraphicsPipeline.RTVFormats[0] = swap->GetDesc().ColorBufferFormat;
	pci.GraphicsPipeline.DSVFormat = swap->GetDesc().DepthBufferFormat;
	pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
	pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
	pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
	{
		auto &rt = pci.GraphicsPipeline.BlendDesc.RenderTargets[0];
		rt.BlendEnable = true;
		rt.SrcBlend  = Diligent::BLEND_FACTOR_SRC_ALPHA;
		rt.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
		rt.BlendOp = Diligent::BLEND_OPERATION_ADD;
		rt.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
		rt.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
		rt.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
	}
	pci.GraphicsPipeline.InputLayout.LayoutElements = layout;
	pci.GraphicsPipeline.InputLayout.NumElements = 4;
	// [rc4l] Without these the pipeline was refused every frame, so no foggy sky was ever hazed.
	pci.pVS = vs;
	pci.pPS = ps;
	dev->CreateGraphicsPipelineState(pci, &g_skyFadePSO);
	return g_skyFadePSO.RawPtr() != nullptr;
}

static void DrawSkyFade(Diligent::IDeviceContext *ctx)
{
	float fr, fg, fb, fa;
	zx::hwrender::GetSkyFog(fr, fg, fb, fa);
	if (fa <= 0.f) return;
	if (!EnsureSkyFadePipeline()) return;

	SkyVertexData v[6];
	const float qx[6] = { -1.f, -1.f,  1.f,  1.f, -1.f,  1.f };
	const float qy[6] = { -1.f,  1.f, -1.f,  1.f,  1.f, -1.f };
	for (int i = 0; i < 6; i++)
	{
		v[i].x = qx[i]; v[i].y = qy[i]; v[i].z = 0.f;
		v[i].u = 0.f; v[i].v = 0.f;
		v[i].r = fr; v[i].g = fg; v[i].b = fb;
		v[i].useTex = -fa;   // the fade VS negates it back into an alpha
		v[i].alpha = 1.f;
	}

	if (!g_skyFadeVB)
	{
		Diligent::BufferDesc bd;
		bd.Name = "fua sky fade VB";
		bd.Size = sizeof(v);
		bd.Usage = Diligent::USAGE_DEFAULT;
		bd.BindFlags = Diligent::BIND_VERTEX_BUFFER;
		GetDevice()->CreateBuffer(bd, nullptr, &g_skyFadeVB);
		if (!g_skyFadeVB) return;
	}
	ctx->UpdateBuffer(g_skyFadeVB, 0, sizeof(v), v,
		Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	Diligent::IBuffer *vbs[] = { g_skyFadeVB };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	ctx->SetPipelineState(g_skyFadePSO);
	Diligent::DrawAttribs d;
	d.NumVertices = 6;
	d.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
	ctx->Draw(d);
}

// Defined below: the world pass, reused verbatim by the sky portal exactly as the mirror pass
// reuses it.

// [rc4l] Which skybox this level takes its sky from, or NULL if the sky is an ordinary one.
//
// GetSkyBox does the DefaultSkybox fallback and honours the per-sector SECF_NO*SKYBOX flags, so
// ask it rather than reading the sector fields by hand. ONE skybox per frame, from the camera
// sector: sectors may each name their own, and rendering one per sector would multiply a full
// scene render by the count -- the shape that had the driver kill this process over the mirror
// pass. See docs/tdr-crash-after-map-loads.md.
static ASkyViewpoint *CurrentSkyViewpoint()
{
	if (!fua_dg_skyportal) return NULL;
	// [rc4l] The viewpoint the VISIBLE SKY SURFACES name, first. GL renders one portal per unique
	// skybox and stencils each through the walls that name it; this backend renders one image, so
	// it has to be the one the sky in view actually belongs to. Falling back to the sector the
	// player stands in gives the right city seen from the wrong room on any map with more than
	// one skybox -- small and off-centre inside a hard-edged rectangle.
	ASkyViewpoint *fromWalls = (ASkyViewpoint *)zx::levelmesh::SkyQuadViewpoint();
	if (fromWalls != NULL) return fromWalls;
	sector_t *sec = (consoleplayer >= 0 && players[consoleplayer].camera != NULL)
		? players[consoleplayer].camera->Sector : NULL;
	// [rc4l] A stacked ceiling's "skybox" is the stack's anchor, as GLWall::SkyPlane knows, and a sky rendered from it is the town seen from a rooftop.
	ASkyViewpoint *sb = (sec != NULL && sec->portals[sector_t::ceiling] == NULL) ? sec->GetSkyBox(sector_t::ceiling) : NULL;
	if (sb == NULL) sb = level.DefaultSkybox;
	return sb;
}

// A full-screen triangle built from the vertex index -- no buffer, no input layout.
static const char *kSkyPortalVS =
	"#version 450\n"
	"layout(location = 0) out vec2 vUV;\n"
	"void main() {\n"
	"    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));\n"
	// [rc4l] V flipped: the portal is sampled by a triangle in clip space, where +Y is DOWN, while
	// the texture it reads was rasterised with row 0 at the top. Sampling straight through hands back
	// a skybox standing on its head -- and one that then appears to swing the wrong way when you look
	// up, which reads as a broken camera rather than a flipped fetch.
	"    vUV = vec2(p.x, 1.0 - p.y);\n"
	"    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

static const char *kSkyPortalPS =
	"#version 450\n"
	"layout(location = 0) in vec2 vUV;\n"
	"layout(binding = 1) uniform sampler2D uTex;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() { outColor = vec4(texture(uTex, vUV).rgb, 1.0); }\n";

// [rc4l] A pipeline that paints one picture, uTex, over its whole target with kSkyPortalVS's triangle and the fragment stage ps, never against depth; dsvFormat is whatever depth buffer is bound beside it.
bool CreatePicturePSO(const char *name, const char *ps, Diligent::FILTER_TYPE filter, Diligent::TEXTURE_FORMAT dsvFormat,
	Diligent::RefCntAutoPtr<Diligent::IPipelineState> &pso)
{
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;
	FString vsName, psName, psoName;
	vsName.Format("%s VS", name); psName.Format("%s PS", name); psoName.Format("%s PSO", name);
	Diligent::RefCntAutoPtr<Diligent::IShader> vs, fs;
	vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, vsName.GetChars(), kSkyPortalVS);
	fs = CompileShader(Diligent::SHADER_TYPE_PIXEL, psName.GetChars(), ps);
	if (!vs || !fs) return false;
	Diligent::SamplerDesc samp;
	samp.MinFilter = samp.MagFilter = samp.MipFilter = filter;
	samp.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
	samp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
	Diligent::ImmutableSamplerDesc samplers[] = { { Diligent::SHADER_TYPE_PIXEL, "uTex", samp } };
	Diligent::ShaderResourceVariableDesc vars[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	Diligent::GraphicsPipelineStateCreateInfo pci;
	pci.PSODesc.Name = psoName.GetChars();
	pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
	pci.GraphicsPipeline.NumRenderTargets = 1;
	pci.GraphicsPipeline.RTVFormats[0] = swap->GetDesc().ColorBufferFormat;
	pci.GraphicsPipeline.DSVFormat = dsvFormat;
	pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
	pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
	pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
	pci.PSODesc.ResourceLayout.Variables = vars;
	pci.PSODesc.ResourceLayout.NumVariables = 1;
	pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
	pci.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
	pci.pVS = vs;
	pci.pPS = fs;
	pso.Release();
	dev->CreateGraphicsPipelineState(pci, &pso);
	return pso.RawPtr() != nullptr;
}

// [rc4l] A surface painted from a picture of the world by its own screen position -- the sky's from the skybox's view, a stacked plane from its view -- and depth-written like any wall.
static const char *kScreenSurfVS =
	"#version 450\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aUV;\n"
	"layout(location = 2) in float aPiece;\n"
	FUA_CONSTANTS_GLSL
	"layout(location = 0) out vec3 vWorld;\n"
	"void main() {\n"
	"    vWorld = aPos;\n"
	"    gl_Position = uMVP * vec4(aPos, 1.0);\n"
	"}\n";
// [rc4l] No cut at the horizon: GL fills a sky wall through its stencil over the whole visible wall, below eye level too, which is what hides what stands behind it.
// [rc4l] A mirror's pass draws these from the main walk, so one beyond the glass, between the reflected eye and the mirror, would paint over the reflection; the clip drops it as it drops the world there.
static const char *kScreenSurfPS =
	"#version 450\n"
	FUA_CONSTANTS_GLSL
	"layout(binding = 1) uniform sampler2D uSky;\n"
	"layout(location = 0) in vec3 vWorld;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() {\n"
	FUA_MIRROR_CLIP_GLSL("vWorld")
	// [rc4l] Sampled by SCREEN FRACTION, not by texel.
	//
	// texelFetch indexes the sky image directly, which is only right while that image is
	// exactly the size of the window. It is not: a side-by-side session gives the Vulkan
	// window its own swapchain, and the portal texture is built from whichever size was
	// current -- so every fragment past the texture edge fetched nothing and the sky came
	// out as a hard-edged RECTANGLE with the map dark around it. It reproduces on no other
	// window size, which is why an embedded capture at 1024x640 never showed it.
	//
	// A fraction is also what makes fua_dg_skyportal_scale work: a half-size portal image
	// still covers the whole screen.
	// [rc4l] Against the SKY IMAGE own size, not uScreen. uScreen is filled by whichever
	// pass wrote the constants last and carries the mirror target size on some paths, which
	// squeezed the whole sky into a fraction of the window -- the skyline sitting in a
	// hard-edged rectangle. The portal texture is built from the swapchain, so its size IS
	// the window size, and asking the sampler is self-consistent by construction.
	"    vec2 uv = gl_FragCoord.xy / vec2(textureSize(uSky, 0));\n"
	"    outColor = vec4(texture(uSky, uv).rgb, 1.0);\n"
	"}\n";

// [rc4l] A fragment stage for a pass that only writes depth, clipped at a mirror as the coloured one is.
static const char *kScreenSurfDepthPS =
	"#version 450\n"
	FUA_CONSTANTS_GLSL
	"layout(location = 0) in vec3 vWorld;\n"
	"void main() {\n"
	FUA_MIRROR_CLIP_GLSL("vWorld")
	"}\n";

// [rc4l] The pipeline for such surfaces: texType says whether uSky is one fixed picture (the sky) or one per binding (each stacked view).
Diligent::RefCntAutoPtr<Diligent::IPipelineState> CreateScreenSurfacePSO(const char *name,
	Diligent::SHADER_RESOURCE_VARIABLE_TYPE texType, bool depthOnly)
{
	Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap || !g_cb) return pso;
	const auto &sd = swap->GetDesc();
	const char *ps = depthOnly ? kScreenSurfDepthPS : kScreenSurfPS;
	Diligent::RefCntAutoPtr<Diligent::IShader> svs, sps;
	svs = CompileShader(Diligent::SHADER_TYPE_VERTEX, name, kScreenSurfVS);
	sps = CompileShader(Diligent::SHADER_TYPE_PIXEL, name, ps);
	if (!svs || !sps) return pso;

	Diligent::ShaderResourceVariableDesc surfVars[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uSky", texType },
		{ Diligent::SHADER_TYPE_PIXEL, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
	};
	// uSky is a GLSL sampler2D -- a COMBINED image sampler. Without an immutable sampler half
	// of that binding is unfilled, and the copy CreateShaderResourceBinding makes then faults
	// inside the Vulkan loader, nowhere near the shader that caused it.
	Diligent::SamplerDesc ssamp;
	ssamp.MinFilter = Diligent::FILTER_TYPE_LINEAR;
	ssamp.MagFilter = Diligent::FILTER_TYPE_LINEAR;
	ssamp.MipFilter = Diligent::FILTER_TYPE_POINT;
	ssamp.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
	ssamp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
	Diligent::ImmutableSamplerDesc surfSamplers[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uSky", ssamp },
	};
	Diligent::LayoutElement selems[] = {
		Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{2, 0, 1, Diligent::VT_FLOAT32, false},
	};
	Diligent::GraphicsPipelineStateCreateInfo pci;
	pci.PSODesc.Name = name;
	pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
	pci.GraphicsPipeline.NumRenderTargets = 1;
	pci.GraphicsPipeline.RTVFormats[0] = sd.ColorBufferFormat;
	pci.GraphicsPipeline.DSVFormat = sd.DepthBufferFormat;
	pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
	ClampDepthLikeGL(pci.GraphicsPipeline);
	pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
	// [rc4l] Depth tested AND written. Measured both ways on three vantages: writing is the
	// better of the two everywhere (the car vantage 9.4%% of pixels against 9.9%%, the alley
	// 20.1%% against 21.1%%), so the sky occluding the passes after it costs less here than
	// leaving them to draw over it.
	pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = true;
	// [rc4l] LESS_EQUAL, and it is the whole thing working or not.
	//
	// A sky surface is COPLANAR with the wall it replaces -- GL draws sky INSTEAD of that
	// wall, not in front of it -- so it arrives at exactly the depth already in the buffer.
	// Under the default LESS that is a fail, every time, and the pass draws nothing at all
	// while looking perfectly correct from the outside: quads captured, runs built, draws
	// submitted, no pixels. GL uses GL_LEQUAL here for the same reason.
	pci.GraphicsPipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
	// [rc4l] NO depth bias. A sky surface is usually COPLANAR with the wall it replaces --
	// GL draws sky instead of that wall, not in front of it -- so biasing these away from
	// the camera makes them lose every such contest and draw nothing at all. A bias was
	// tried when the quads were derived from the map and over-emitted; taking them from GL
	// removes the reason for it, because GL only emits the ones that belong.
	pci.GraphicsPipeline.InputLayout.LayoutElements = selems;
	pci.GraphicsPipeline.InputLayout.NumElements = 3;
	pci.PSODesc.ResourceLayout.Variables = surfVars;
	pci.PSODesc.ResourceLayout.NumVariables = 2;
	pci.PSODesc.ResourceLayout.ImmutableSamplers = surfSamplers;
	pci.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
	if (depthOnly)
	{
		// [rc4l] A depth-only pass reads no picture, so it declares just the constants, surfVars' second entry.
		pci.PSODesc.ResourceLayout.Variables = &surfVars[1];
		pci.PSODesc.ResourceLayout.NumVariables = 1;
		pci.PSODesc.ResourceLayout.NumImmutableSamplers = 0;
		pci.GraphicsPipeline.BlendDesc.RenderTargets[0].RenderTargetWriteMask = Diligent::COLOR_MASK_NONE;
	}
	pci.pVS = svs;
	pci.pPS = sps;
	dev->CreateGraphicsPipelineState(pci, &pso);
	if (!pso) return pso;
	if (!BindSceneConstants(pso))
	{
		Printf("vulkan %s OFF: pipeline missing its constants\n", name);
		pso.Release();
	}
	return pso;
}

bool EnsureSkyPortalResources()
{
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;

	// [rc4l] FULL resolution, after half was tried and looked like it.
	//
	// The reasoning for halving it was that a skybox sits at infinity and is barely magnified. That
	// is true of a sky TEXTURE and false of this: a sky portal is a view of a city, full of thin
	// bright detail, and its pixels land on screen at 1:1 like any others. Halved, the skyline was
	// visibly softer than GL's beside it -- which is what rc4l reported, in those words.
	//
	// The cost it was bounding is real (one extra scene render per frame, the shape that produced
	// the TDR in tdr-crash-after-map-loads.md), so the divisor stays reachable rather than deleted:
	// fua_dg_skyportal_scale 2 halves it again if a map ever needs the frame time back.
	int div = fua_dg_skyportal_scale;
	if (div < 1) div = 1;
	if (div > 4) div = 4;
	const int w = FrameWidth() / div, h = FrameHeight() / div;
	if (w <= 0 || h <= 0) return false;
	if (!g_skyPortalColor || g_skyPortalW != w || g_skyPortalH != h)
	{
		g_skyPortalColor.Release();
		g_skyPortalDepth.Release();
		g_skyPortalSRB.Release();
		g_skySurfPSO.Release();
		g_skySurfSRB.Release();
		if (!CreateViewTextures(w, h, "fua sky portal", g_skyPortalColor, g_skyPortalDepth)) return false;
		g_skyPortalW = w; g_skyPortalH = h;
	}

	// [rc4l] Same standing as the dome it replaces: drawn first, never against the depth buffer, into the frame's own targets.
	if (!g_skyPortalPSO && !CreatePicturePSO("fua sky portal", kSkyPortalPS, Diligent::FILTER_TYPE_LINEAR, swap->GetDesc().DepthBufferFormat, g_skyPortalPSO))
		return false;
	if (!g_skySurfPSO)
	{
		g_skySurfPSO = CreateScreenSurfacePSO("fua sky surface", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC);
		auto *srv = g_skyPortalColor->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
		auto *tv = g_skySurfPSO ? g_skySurfPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "uSky") : NULL;
		if (tv && srv)
		{
			tv->Set(srv);
			g_skySurfSRB.Release();
			g_skySurfPSO->CreateShaderResourceBinding(&g_skySurfSRB, true);
		}
		else if (g_skySurfPSO)
		{
			Printf("vulkan sky surfaces OFF: pipeline missing its texture\n");
			g_skySurfPSO.Release();
		}
	}
	if (!g_skyPortalSRB && !CreatePictureBinding(g_skyPortalPSO, "uTex", g_skyPortalColor, g_skyPortalSRB)) return false;
	return true;
}

// Render the world from the skybox viewpoint, then paint it as this frame background.
// [rc4l] Rendered before the frame's own world pass, exactly as the camera-texture pass is. The sky
// portal is a second view of the map, and a second view cannot be taken from inside the first: the
// world draw derives its visible set and light clusters once per call, so re-entering it hands the
// outer pass the skybox room's geometry to draw with the player's camera -- and the map disappears.
bool RenderSkyPortal(Diligent::IDeviceContext *ctx)
{
	g_skyPortalActive = false;
	if (!fua_dg_skyportal) return false;
	ASkyViewpoint *sb = CurrentSkyViewpoint();
	if (sb == NULL) return false;
	if (!EnsureSkyPortalResources()) return false;

	auto *brtv = FrameRTV();
	// [rc4l] The world pass renders into our own readable depth buffer, so restoring the frame's
	// here would hand the screen a depth buffer nothing has written to.
	Diligent::ITextureView *bdsv = EnsureSceneDepth();
	if (!bdsv) bdsv = FrameDSV();
	auto *prtv = g_skyPortalColor->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
	auto *pdsv = g_skyPortalDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
	if (!prtv || !pdsv) return false;

	const fixed_t sx = viewx, sy = viewy, sz = viewz;
	const angle_t sa = viewangle;
	float savedMVP[16];
	for (int i = 0; i < 16; i++) savedMVP[i] = g_mvp[i];

	// [rc4l] The viewpoint, interpolated, with its own angle ADDED to the camera -- which is how a
	// skybox parallaxes and rotates. The BAM interpolation is the tested one; a hand-rolled version
	// is already recorded in this tree as having made a rotating portal overshoot on right turns.
	// [rc4l] Interpolated ONLY when the viewpoint has a previous position to interpolate from.
	//
	// A skybox viewpoint that never moves may carry PrevX/Y/Z of zero -- nothing has ever
	// written them -- and then this lerps from the MAP ORIGIN to the actor, landing the camera
	// somewhere out in the middle of the level and moving it with r_TicFrac. The skybox is then
	// rendered from the wrong place, which makes the city small and off-centre, and it shifts
	// as the frame fraction changes: the "sky moves with the mouse" this started from.
	const fixed_t frac = r_TicFrac;
	const bool moved = (sb->PrevX != 0 || sb->PrevY != 0 || sb->PrevZ != 0);
	viewx = moved ? sb->PrevX + FixedMul(frac, sb->x - sb->PrevX) : sb->x;
	viewy = moved ? sb->PrevY + FixedMul(frac, sb->y - sb->PrevY) : sb->y;
	viewz = moved ? sb->PrevZ + FixedMul(frac, sb->z - sb->PrevZ) : sb->z;
	viewangle = sa + (moved ? zx::InterpolateAngleBAM(sb->PrevAngle, sb->angle, (int64_t)frac)
	                        : sb->angle);

	// Never let the eye sit inside the skybox room floor or ceiling.
	if (sb->Sector != NULL)
	{
		const fixed_t fl = sb->Sector->floorplane.ZatPoint(sb->x, sb->y);
		const fixed_t ce = sb->Sector->ceilingplane.ZatPoint(sb->x, sb->y);
		if (viewz < fl + 4 * FRACUNIT) viewz = fl + 4 * FRACUNIT;
		if (viewz > ce - 4 * FRACUNIT) viewz = ce - 4 * FRACUNIT;
	}

	// [rc4l] Where the portal camera actually stands. Recorded HERE and not after the restore a
	// few lines down, which hands back the player position -- the reach test then measured from
	// the player, excluded the skybox room along with everything else, and drew nothing at all.
	g_pass.skyCamX = FIXED2FLOAT(viewx);
	g_pass.skyCamY = FIXED2FLOAT(viewy);
	g_pass.skyCamZ = FIXED2FLOAT(viewz);
	BuildMVP(g_mvp);
	viewx = sx; viewy = sy; viewz = sz; viewangle = sa;

	ctx->SetRenderTargets(1, &prtv, pdsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	const float clearCol[4] = { 0.f, 0.f, 0.f, 1.f };
	ctx->ClearRenderTarget(prtv, clearCol, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->ClearDepthStencil(pdsv, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0,
		Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	{
		Diligent::Viewport vp;
		vp.TopLeftX = 0; vp.TopLeftY = 0;
		vp.Width = (float)g_skyPortalW; vp.Height = (float)g_skyPortalH;
		vp.MinDepth = 0.f; vp.MaxDepth = 1.f;
		ctx->SetViewports(1, &vp, (Diligent::Uint32)g_skyPortalW, (Diligent::Uint32)g_skyPortalH);
	}
	if (g_cb)
	{
		Diligent::MapHelper<float> cb(ctx, g_cb, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		for (int k = 0; k < 16; k++) cb[k] = g_mvp[k];
	}
	g_pass.inSkyPortal = true;
	DrawWorld(ctx);
	g_pass.inSkyPortal = false;

	// Hand the screen back its own target and full viewport; the frame assumes both.
	for (int k = 0; k < 16; k++) g_mvp[k] = savedMVP[k];
	ctx->SetRenderTargets(1, &brtv, bdsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->SetViewports(1, nullptr, 0, 0);
	if (g_cb)
	{
		Diligent::MapHelper<float> cb(ctx, g_cb, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		for (int k = 0; k < 16; k++) cb[k] = g_mvp[k];
	}
	// [rc4l] The picture changes hands HERE, outside the frame's render pass, not at the draw that
	// samples it. A transition asked for mid-pass makes Diligent end the pass to place the barrier and
	// begin a new one, and the world drawn afterwards lands in a pass the frame no longer owns -- which
	// is how a working portal blit came to erase the map behind it.
	{
		Diligent::StateTransitionDesc barrier(g_skyPortalColor, Diligent::RESOURCE_STATE_UNKNOWN,
			Diligent::RESOURCE_STATE_SHADER_RESOURCE, Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE);
		ctx->TransitionResourceStates(1, &barrier);
	}
	g_skyPortalActive = true;
	return true;
}

// [rc4l] See dgshared.h: the portal pass's camera, for passes that apply the same reach as the world.
bool SkyPortalReach(float &camX, float &camY, float &reach)
{
	if (!g_pass.inSkyPortal || fua_dg_skyportal_reach <= 0.f) return false;
	camX = g_pass.skyCamX;
	camY = g_pass.skyCamY;
	reach = fua_dg_skyportal_reach;
	return true;
}

// [rc4l] What the viewpoint saw, laid down where the sky would otherwise be. Depth stays open, so
// the world draws over it in the same pass -- the portal is a background, not geometry.
static bool BlitSkyPortal(Diligent::IDeviceContext *ctx)
{
	if (!g_skyPortalActive || !g_skyPortalPSO || !g_skyPortalSRB) return false;
	ctx->SetPipelineState(g_skyPortalPSO);
	ctx->CommitShaderResources(g_skyPortalSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
	Diligent::DrawAttribs dr;
	dr.NumVertices = 3;
	dr.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
	ctx->Draw(dr);
	return true;
}

static void DrawSky(Diligent::IDeviceContext *ctx)
{
	// [rc4l] The box is one buffer and a draw per face, because each face is its own material -- the
	// same shape as a world batch, which is one draw per binding.
	if (g_skyIsBox)
	{
		if (!g_skyBoxVB || !g_skyBoxPSO || g_skyBoxFaces.Size() == 0) return;
		// [rc4l] RenderBox turns the box by the sky's scroll as RenderDome turns the dome.
		SetSkyDraw(ctx, g_defaultLook.xOffset, false, 0.f, NULL, false);
		Diligent::IBuffer *bvbs[] = { g_skyBoxVB };
		const Diligent::Uint64 boffsets[] = { 0 };
		ctx->SetVertexBuffers(0, 1, bvbs, boffsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
			Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
		ctx->SetPipelineState(g_skyBoxPSO);
		for (unsigned i = 0; i < g_skyBoxFaces.Size(); i++)
		{
			const SkyBoxFace &f = g_skyBoxFaces[i];
			if (!f.srb || f.count == 0) continue;
			ctx->CommitShaderResources(f.srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			Diligent::DrawAttribs d;
			d.NumVertices = (Diligent::Uint32)f.count;
			d.StartVertexLocation = (Diligent::Uint32)f.first;
			d.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
			ctx->Draw(d);
		}
		// [rc4l] No fade over a sky box, as GL's RenderBox draws none.
		return;
	}

	DrawLook(ctx, g_defaultLook, g_defaultDome, g_secondDome);
}


// [rc4l] The sky as depth-writing surfaces -- GL's captured quads plus the horizon lines' -- painted from the sky image by screen position.
// [rc4l] Uploads a frame's screen-sampled surfaces and binds them under pso, ready for a draw per picture; false when there is nothing to draw.
bool BindScreenSurfaces(Diligent::IDeviceContext *ctx, Diligent::RefCntAutoPtr<Diligent::IBuffer> &vb,
	unsigned int &capacity, const TArray<SceneVertex> &sv, Diligent::IPipelineState *pso, const char *name)
{
	if (sv.Size() < 3 || !UploadGrowingVB(ctx, vb, capacity, &sv[0], sv.Size(), sizeof(SceneVertex), 256, name)) return false;
	Diligent::IBuffer *vbs[] = { vb };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	ctx->SetPipelineState(pso);
	return true;
}

// [rc4l] One run of bound surfaces, painted from the picture srb carries.
void DrawScreenSurfaceRun(Diligent::IDeviceContext *ctx, Diligent::IShaderResourceBinding *srb, unsigned int first, unsigned int count)
{
	ctx->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
	Diligent::DrawAttribs dr;
	dr.NumVertices = count;
	dr.StartVertexLocation = first;
	dr.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
	ctx->Draw(dr);
}

// [rc4l] This pass's sky surfaces: GL's own sky quads for the view being drawn, plus the sky halves of the horizon lines, which come from the map.
static void GatherSkySurfaces(TArray<SceneVertex> &sv)
{
	int nverts = 0, nhorizon = 0;
	const zx::levelmesh::MeshVertex *src = zx::levelmesh::SkyQuadVertices(nverts,
		zx::levelmesh::ComputeSkyDrawList(g_pass.inCameraPass, g_pass.stackSlot));
	// [rc4l] A horizon line is built for the player's eye, so a stacked view, which stands somewhere else, takes none; the sky starts at this pass's eye, where the horizon pass stops the floor half.
	float eye[3];
	const zx::levelmesh::MeshVertex *hsrc = (g_pass.stackSlot < 0 && zx::horizon::ComputeEyeFromViewProjection(g_mvp, eye))
		? zx::horizon::HorizonSkyVertices(nhorizon, eye[1]) : NULL;
	if (src == NULL) nverts = 0;
	if (hsrc == NULL) nhorizon = 0;
	// [rc4l] Sized once and filled, as a Push a vertex was 3% of a Sunder MAP14 frame's game thread.
	sv.Resize((unsigned)(nverts + nhorizon));
	SceneVertex *out = sv.Size() > 0 ? &sv[0] : NULL;
	for (int i = 0; i < nverts; i++) out[i] = ToSceneVertex(src[i], 0);
	for (int i = 0; i < nhorizon; i++) out[nverts + i] = ToSceneVertex(hsrc[i], 0);
}

// [rc4l] Under a plain sky dome, the same surfaces in depth only, before the world: GL's sky walls reach to 32768 and hide whatever stands behind them, and the dome is already the colour there.
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_skyOccluderPSO;
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_skyOccluderSRB;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_skyOccluderVB;
static unsigned int g_skyOccluderVBCapacity = 0;

static void DrawSkyOccluders(Diligent::IDeviceContext *ctx)
{
	if (g_skyPortalActive) return;
	if (!g_skyOccluderPSO)
	{
		g_skyOccluderPSO = CreateScreenSurfacePSO("fua sky occluder", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC, true);
		if (!g_skyOccluderPSO) return;
		g_skyOccluderPSO->CreateShaderResourceBinding(&g_skyOccluderSRB, true);
	}
	if (!g_skyOccluderSRB) return;
	static TArray<SceneVertex> sv;
	GatherSkySurfaces(sv);
	if (!BindScreenSurfaces(ctx, g_skyOccluderVB, g_skyOccluderVBCapacity, sv, g_skyOccluderPSO, "fua sky occluder VB")) return;
	DrawScreenSurfaceRun(ctx, g_skyOccluderSRB, 0, sv.Size());
}

// [rc4l] Skies other than the level's that this frame's sky surfaces show (a line's, sky2), each drawn into a picture of its own and painted onto its surfaces by screen position, as GL draws each GLSkyInfo through a portal of its own.
struct SkyLookPicture
{
	zx::hwrender::SkyLook look;
	Diligent::RefCntAutoPtr<Diligent::ITexture> color, depth;
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
	int w = 0, h = 0;
};
static const int kSkyLookPictures = 4;
static SkyLookPicture g_lookPictures[kSkyLookPictures];
static int g_lookPictureCount = 0;
// [rc4l] Each sky key the player's sky surfaces carry this frame, and the picture it paints from, -1 for the level's sky.
struct SkyKeyPicture { int key, picture; };
static TArray<SkyKeyPicture> g_keyPictures;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_lookSurfPSO;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_lookSurfVB;
static unsigned int g_lookSurfVBCapacity = 0;

static void ReleaseSkyLookPictures()
{
	for (int i = 0; i < kSkyLookPictures; i++) g_lookPictures[i] = SkyLookPicture();
	g_lookPictureCount = 0;
	g_keyPictures.Clear();
	g_lookSurfPSO.Release();
}

// [rc4l] Where key stands in this frame's list, or -1 when no surface carries it.
static int FindSkyKey(int key)
{
	for (unsigned i = 0; i < g_keyPictures.Size(); i++) if (g_keyPictures[i].key == key) return (int)i;
	return -1;
}

void RenderSkyLooks(Diligent::IDeviceContext *ctx)
{
	g_lookPictureCount = 0;
	g_keyPictures.Clear();
	if (!fua_dg_sky || g_skyPortalActive || !EnsureSkyPipeline()) return;
	int nverts = 0;
	const zx::levelmesh::MeshVertex *q = zx::levelmesh::SkyQuadVertices(nverts, zx::levelmesh::kSkyCaptureMain);
	if (q == NULL) return;
	const zx::hwrender::SkyLook level = LookOfSky(0);
	for (int i = 0; i + 5 < nverts; i += 6)
	{
		const int key = (int)q[i].u;
		if (key == 0 || FindSkyKey(key) >= 0) continue;
		const zx::hwrender::SkyLook look = LookOfSky(zx::hwrender::ComputeSkyFromQuadKey(key));
		int picture = -1;
		if (!zx::hwrender::ComputeSameSkyLook(look, level))
		{
			for (int p = 0; p < g_lookPictureCount && picture < 0; p++)
				if (zx::hwrender::ComputeSameSkyLook(look, g_lookPictures[p].look)) picture = p;
			if (picture < 0 && g_lookPictureCount < kSkyLookPictures) { picture = g_lookPictureCount++; g_lookPictures[picture].look = look; }
		}
		const SkyKeyPicture kp = { key, picture };
		g_keyPictures.Push(kp);
	}
	if (g_lookPictureCount == 0) return;
	if (!g_lookSurfPSO) g_lookSurfPSO = CreateScreenSurfacePSO("fua sky look surface", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);
	if (!g_lookSurfPSO) { g_lookPictureCount = 0; return; }

	auto *brtv = FrameRTV();
	Diligent::ITextureView *bdsv = EnsureSceneDepth();
	if (!bdsv) bdsv = FrameDSV();
	const int w = FrameWidth(), h = FrameHeight();
	// [rc4l] The player's matrix and the frame's colormap, which the dome shaders read from the scene block.
	{
		Diligent::MapHelper<float> cb(ctx, g_cb, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		WriteSceneConstants(&cb[0], nullptr, (float)w, (float)h, BindlessActive() ? (float)MaterialSlotCount() : 0.f);
	}
	for (int p = 0; p < g_lookPictureCount; p++)
	{
		SkyLookPicture &pic = g_lookPictures[p];
		if (!pic.color || pic.w != w || pic.h != h)
		{
			if (!CreateViewTextures(w, h, "fua sky look", pic.color, pic.depth) ||
				!CreatePictureBinding(g_lookSurfPSO, "uSky", pic.color, pic.srb)) { g_lookPictureCount = p; break; }
			pic.w = w; pic.h = h;
		}
		auto *prtv = pic.color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
		auto *pdsv = pic.depth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
		ctx->SetRenderTargets(1, &prtv, pdsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		const float clearCol[4] = { 0.f, 0.f, 0.f, 1.f };
		ctx->ClearRenderTarget(prtv, clearCol, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		const SkyDome *second = pic.look.secondTexture >= 0 ? DomeFor(pic.look.secondTexture) : NULL;
		DrawLook(ctx, pic.look, DomeFor(pic.look.texture), second);
	}
	ctx->SetRenderTargets(1, &brtv, bdsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	// [rc4l] Each picture changes hands here, outside the frame's render pass, for the reason RenderSkyPortal gives.
	for (int p = 0; p < g_lookPictureCount; p++)
	{
		Diligent::StateTransitionDesc barrier(g_lookPictures[p].color, Diligent::RESOURCE_STATE_UNKNOWN,
			Diligent::RESOURCE_STATE_SHADER_RESOURCE, Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE);
		ctx->TransitionResourceStates(1, &barrier);
	}
}

// [rc4l] The player's sky surfaces that show another sky than the level's, each run painted from its look's picture.
static void DrawSkyLookSurfaces(Diligent::IDeviceContext *ctx)
{
	if (g_lookPictureCount == 0 || !g_lookSurfPSO) return;
	if (!g_pass.mainView || g_pass.lineMirror >= 0 || g_pass.stackSlot >= 0 || g_pass.inCameraPass || g_pass.inSkyPortal) return;
	int nverts = 0;
	const zx::levelmesh::MeshVertex *q = zx::levelmesh::SkyQuadVertices(nverts, zx::levelmesh::kSkyCaptureMain);
	if (q == NULL) return;
	static TArray<SceneVertex> sv;
	unsigned int first[kSkyLookPictures], count[kSkyLookPictures];
	sv.Clear();
	for (int p = 0; p < g_lookPictureCount; p++)
	{
		first[p] = sv.Size();
		for (int i = 0; i + 5 < nverts; i += 6)
		{
			const int at = FindSkyKey((int)q[i].u);
			if (at < 0 || g_keyPictures[at].picture != p) continue;
			for (int k = 0; k < 6; k++) sv.Push(ToSceneVertex(q[i + k], 0));
		}
		count[p] = sv.Size() - first[p];
	}
	if (!BindScreenSurfaces(ctx, g_lookSurfVB, g_lookSurfVBCapacity, sv, g_lookSurfPSO, "fua sky look surface VB")) return;
	for (int p = 0; p < g_lookPictureCount; p++)
		if (count[p] > 0 && g_lookPictures[p].srb) DrawScreenSurfaceRun(ctx, g_lookPictures[p].srb, first[p], count[p]);
	g_skyDraws++;
}

void DrawSkySurfaces(Diligent::IDeviceContext *ctx)
{
	DrawSkyLookSurfaces(ctx);
	if (!g_skyPortalActive || !g_skySurfPSO || !g_skySurfSRB) return;

	// [rc4l] Its own buffer, rebuilt each frame, since GL clears the shared dynamic stream when it re-enters its scene builder for the skybox portal, between the walk that produces these and the draw.
	static TArray<SceneVertex> sv;
	GatherSkySurfaces(sv);
	if (!BindScreenSurfaces(ctx, g_skySurfVB, g_skySurfVBCapacity, sv, g_skySurfPSO, "fua sky surface VB")) return;
	DrawScreenSurfaceRun(ctx, g_skySurfSRB, 0, sv.Size());
	g_skyDraws++;
}
// [rc4l] The sky's pipelines and bindings, released with the scene's when a level or a filter change rebuilds them.
void ReleaseSkyPipelines()
{
	for (int i = 0; i < kSkyDomes; i++) g_domes[i] = SkyDome();
	g_defaultDome = g_secondDome = NULL;
	ReleaseSkyLookPictures();
	g_skyPSO.Release();
	// The face bindings are keyed on this level's materials, so they go with everything else that is.
	g_skyBoxFaces.Clear();
	g_skyBoxVB.Release();
	g_skyBoxPSO.Release();
	g_skyIsBox = false;
	g_skyPortalSRB.Release();
	g_skyPortalPSO.Release();
	g_skyPortalColor.Release();
	g_skyPortalDepth.Release();
	g_skyPortalW = g_skyPortalH = 0;
	g_skyFadePSO.Release();
	g_skyOccluderSRB.Release();
	g_skyOccluderPSO.Release();
	g_boxMaterial = NULL;
}

// [rc4l] A new level's sky is built again from its own textures.
void ResetSkyForLevel()
{
	for (int i = 0; i < kSkyDomes; i++) { g_domes[i].valid = false; g_domes[i].texture = -1; g_domes[i].material = nullptr; }
	g_defaultDome = g_secondDome = NULL;
	g_boxMaterial = NULL;
}

// [rc4l] The sky behind the world, painted first with depth off: the skybox's picture, or the dome and its occluders.
void DrawSkyBackground(Diligent::IDeviceContext *ctx)
{
	// [rc4l] The level's sky scrolls with gl_frameMS, so its look is read every frame.
	g_defaultLook = LookOfSky(0);
	g_skyAngle = (-180.0f + g_defaultLook.xOffset) * 3.14159265f / 180.0f;
	// [rc4l] A sky that is a PLACE takes precedence over any texture.
	//
	// When the map has an ASkyViewpoint, the sky is the world seen from it and there is no sky
	// texture to speak of -- maps that use one leave sky1texture as the null placeholder, which
	// is why this used to draw a blue-and-white checkerboard here.
	// [rc4l] Painted full-screen first, as GL's RenderFirstSkyPortal does, so sky no captured wall covers is still sky; the surfaces drawn later only cut what lies beyond it.
	if (!BlitSkyPortal(ctx))
	{
		EnsureSky();
		DrawSky(ctx);
		DrawSkyOccluders(ctx);
	}
}

// [rc4l] What the scene constants carry of the sky: its scroll angle, the dome's texture scales and its top cap's colour.
void GetSkyConstants(float &angle, float &xScale, float &vScale, unsigned char &capR, unsigned char &capG, unsigned char &capB)
{
	angle = g_skyAngle;
	const SkyDome *d = g_defaultDome;
	xScale = d != NULL ? d->xscale : 4.f; vScale = d != NULL ? d->vscale : 1.f;
	capR = d != NULL ? d->cap[0].r : 0; capG = d != NULL ? d->cap[0].g : 0; capB = d != NULL ? d->cap[0].b : 0;
}

int SkyDraws() { return g_skyDraws; }

// [rc4l] The pipeline that paints one picture over a whole target, which the stacked-view display borrows.
Diligent::IPipelineState *SkyPortalPicturePSO() { return g_skyPortalPSO; }

}} // namespace zx::hwrender
