// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The level's sky: GL's dome or skybox and the fade, with the skybox portal in dgskyportal.cpp and other skies' pictures in dgskylooks.cpp.

#include "features/hwrender/diligent/dgsky_internal.h"
#include "features/viewsetup/viewsetup.h"   // [rc4l] the sky's scroll this frame
#include "g_level.h"   // [rc4l] level.flags, the double and swapped skies
#include "r_sky.h"     // [rc4l] sky1texture and sky2texture
#include "features/telemetry/telemetry.h"

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

static int g_skyDraws = 0;

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
	"layout(binding = 1) uniform texture2D uTex; uniform sampler uTex_sampler;\n"
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
	"        vec4 t = texture(sampler2D(uTex, uTex_sampler), vUV);\n"
	"        if (sel < 0.5 || t.a < 0.05) discard;\n"
	"        outColor = vec4(uFixedColormap.w > 0.5 ? fuaFixedColormap(t.rgb) : t.rgb, t.a * vSkyAlpha);\n"
	"        return;\n"
	"    }\n"
	"    float al = vSkyColor.a < 0.0 ? -vSkyColor.a : 1.0;\n"
	"    vec3 c = mix(vSkyColor.rgb, texture(sampler2D(uTex, uTex_sampler), vUV).rgb, sel);\n"
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
	CreateDeviceBuffer(bd, &bdata, &d.vb);
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
		CreateDeviceBuffer(cbd, nullptr, &g_skyDrawCB);
		if (!g_skyDrawCB) return false;
	}
	CreatePipeline(pci, &outPSO);
	if (!outPSO) return false;
	BindSceneConstants(outPSO);
	for (Diligent::SHADER_TYPE stage : { Diligent::SHADER_TYPE_VERTEX, Diligent::SHADER_TYPE_PIXEL })
		if (auto *v = outPSO->GetStaticVariableByName(stage, "SkyDraw")) v->Set(g_skyDrawCB);
	return true;
}

bool EnsureSkyPipeline()
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

zx::hwrender::SkyLook LookOfSky(int sectorSky)
{
	zx::hwrender::SkyLookIn in = {};
	in.sectorSky = sectorSky;
	const int line = (sectorSky & (zx::hwrender::kSkyFlatFlag - 1)) - 1;
	if ((sectorSky & zx::hwrender::kSkyFlatFlag) && line >= 0 && line < numlines && lines[line].sidedef[0] != NULL)
	{
		const side_t *s = lines[line].sidedef[0];
		const FTextureID top = s->rc.GetTexture(side_t::top), bottom = s->rc.GetTexture(side_t::bottom);
		in.lineTop = top.GetIndex(); in.lineBottom = bottom.GetIndex();
		in.lineTopDraws = SkyTextureDraws(top);
		in.lineBottomValid = bottom.isValid();
		in.lineBottomDraws = SkyTextureDraws(bottom);
		// [rc4l] ANGLE_TO_FLOAT: the x offset read as an angle, in degrees.
		in.lineTopX = (float)(s->rc.GetTextureXOffset(side_t::top).Raw() * 180. / ANGLE_180);
		in.lineBottomX = (float)(s->rc.GetTextureXOffset(side_t::bottom).Raw() * 180. / ANGLE_180);
		in.lineTopY = FIXED2FLOAT(s->rc.GetTextureYOffset(side_t::top));
		in.lineBottomY = FIXED2FLOAT(s->rc.GetTextureYOffset(side_t::bottom));
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

// [rc4l] From the slot already holding texture or else a free one, the last slot taken over when none is.
SkyDome *DomeFor(int texture)
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

void DrawLook(Diligent::IDeviceContext *ctx, const zx::hwrender::SkyLook &look, const SkyDome *main, const SkyDome *second)
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
	CreateDeviceBuffer(bd, &bdata, &g_skyBoxVB);
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
			// [rc4l] A face's name as the C string Printf takes, or "-" for a face the box lacks.
			auto face = [sb](int i) { return sb->faces[i] != NULL ? sb->faces[i]->Name.GetChars() : "-"; };
			Printf("vulkan sky: %s  [SKYBOX, %s: %s %s %s %s %s %s]\n",
				info.name,
				(sb->faces[5] != NULL) ? "six faces" : "wrapped",
				face(0), face(1), face(2), face(3), face(4), face(5));
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
	CreatePipeline(pci, &g_skyFadePSO);
	return g_skyFadePSO.RawPtr() != nullptr;
}

static void DrawSkyFade(Diligent::IDeviceContext *ctx)
{
	float fr, fg, fb, fa;
	zx::visibility::GetSkyFog(fr, fg, fb, fa);
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
		CreateDeviceBuffer(bd, nullptr, &g_skyFadeVB);
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

void DrawSkySurfaces(Diligent::IDeviceContext *ctx)
{
	if (DrawSkyLookSurfaces(ctx)) g_skyDraws++;
	if (DrawSkyPortalSurfaces(ctx)) g_skyDraws++;
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
	ReleaseSkyPortal();
	g_skyFadePSO.Release();
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
	ZX_ZONE("draw/sky");   // [rc4l] the sky behind the world
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

}} // namespace zx::hwrender
