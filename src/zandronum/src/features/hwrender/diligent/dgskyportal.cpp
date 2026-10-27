// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The skybox portal: the world rendered from a sky viewpoint into the picture that is the sky, the pipelines that paint a picture over a target or onto surfaces by screen position, and the sky surfaces and occluders cut from the world.

#include "features/hwrender/diligent/dgsky_internal.h"
#include <vector>
#include "features/gamecopy/players.h"   // [rc4l] the sky viewpoints as the render copy holds them

namespace zx { namespace hwrender {

// [rc4l] ZDoom's other skybox is a place, an ASkyViewpoint whose view of the world is the sky, rendered like a mirror into a picture painted first as the frame's background.
static Diligent::RefCntAutoPtr<Diligent::ITexture>       g_skyPortalColor, g_skyPortalDepth;
// [rc4l] The normals the skybox view's G-buffer writes and its decal pass reads, at its own scale.
static Diligent::RefCntAutoPtr<Diligent::ITexture>       g_skyPortalNormal;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_skyPortalPSO;
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_skyPortalSRB;
static int  g_skyPortalW = 0, g_skyPortalH = 0;
static bool g_skyPortalActive = false;   // [rc4l] this frame's sky is the portal's picture
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_skySurfPSO;
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_skySurfSRB;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_skySurfVB;
static unsigned int g_skySurfVBCapacity = 0;

// [rc4l] The one skybox this frame's sky comes from, or NULL for an ordinary sky, since rendering one per sector multiplies a full scene render by the count (docs/tdr-crash-after-map-loads.md).
static const zx::gamecopy::ActorRecord *CurrentSkyViewpoint()
{
	if (!fua_dg_skyportal) return NULL;
	// [rc4l] The viewpoint the visible sky surfaces name comes first, since the camera sector's shows the right city from the wrong room on a map with several skyboxes.
	const zx::gamecopy::ActorRecord *fromWalls = zx::gamecopy::Record(zx::levelmesh::SkyQuadViewpoint());
	if (fromWalls != NULL) return fromWalls;
	const zx::gamecopy::ActorRecord *const camera = zx::gamecopy::CameraOf(consoleplayer);
	sector_t *sec = camera != NULL ? camera->Sector : NULL;
	// [rc4l] A stacked ceiling's "skybox" is the stack's anchor, as GLWall::SkyPlane knows, and a sky rendered from it is the town seen from a rooftop.
	const zx::gamecopy::ActorRecord *sb = (sec != NULL && sec->portals[sector_t::ceiling] == NULL) ? zx::gamecopy::Record(zx::gamecopy::SkyBoxOf(sec, sector_t::ceiling)) : NULL;
	if (sb == NULL) sb = zx::gamecopy::Record(zx::gamecopy::DefaultSkyBox());
	return sb;
}

// [rc4l] A full-screen triangle built from the vertex index, with no buffer and no input layout.
static const char *kSkyPortalVS =
	"#version 450\n"
	"layout(location = 0) out vec2 vUV;\n"
	"void main() {\n"
	"    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));\n"
	// [rc4l] V is flipped, since clip space's +Y is down while the picture was rasterised with row 0 at the top, which stood the skybox on its head.
	"    vUV = vec2(p.x, 1.0 - p.y);\n"
	"    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

static const char *kSkyPortalPS =
	"#version 450\n"
	"layout(location = 0) in vec2 vUV;\n"
	"layout(binding = 1) uniform texture2D uTex; uniform sampler uTex_sampler;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() { outColor = vec4(texture(sampler2D(uTex, uTex_sampler), vUV).rgb, 1.0); }\n";

const char *PictureVS() { return kSkyPortalVS; }

// [rc4l] A pipeline that paints one picture, uTex, over its whole target with kSkyPortalVS's triangle and the fragment stage ps, never against depth; dsvFormat is whatever depth buffer is bound beside it.
bool CreatePicturePSO(const char *name, const char *ps, Diligent::FILTER_TYPE filter, Diligent::TEXTURE_FORMAT dsvFormat,
	Diligent::RefCntAutoPtr<Diligent::IPipelineState> &pso)
{
	static const char *const kPicture[] = { "uTex" };
	return CreatePicturesPSO(name, ps, filter, dsvFormat, kPicture, 1, pso);
}

bool CreatePicturesPSO(const char *name, const char *ps, Diligent::FILTER_TYPE filter, Diligent::TEXTURE_FORMAT dsvFormat,
	const char *const *pictures, int count, Diligent::RefCntAutoPtr<Diligent::IPipelineState> &pso)
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
	std::vector<Diligent::ImmutableSamplerDesc> samplers;
	std::vector<Diligent::ShaderResourceVariableDesc> vars;
	for (int i = 0; i < count; i++)
	{
		samplers.push_back(Diligent::ImmutableSamplerDesc(Diligent::SHADER_TYPE_PIXEL, pictures[i], samp));
		vars.push_back(Diligent::ShaderResourceVariableDesc(Diligent::SHADER_TYPE_PIXEL, pictures[i], Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE));
	}
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
	pci.PSODesc.ResourceLayout.Variables = vars.data();
	pci.PSODesc.ResourceLayout.NumVariables = (Diligent::Uint32)vars.size();
	pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers.data();
	pci.PSODesc.ResourceLayout.NumImmutableSamplers = (Diligent::Uint32)samplers.size();
	pci.pVS = vs;
	pci.pPS = fs;
	pso.Release();
	CreatePipeline(pci, &pso);
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
	"layout(binding = 1) uniform texture2D uSky; uniform sampler uSky_sampler;\n"
	"layout(location = 0) in vec3 vWorld;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() {\n"
	FUA_MIRROR_CLIP_GLSL("vWorld")
	// [rc4l] Sampled by screen fraction against the sky picture's own size, since a texel fetch or uScreen is wrong whenever the picture is not the window's size (a side-by-side window, a mirror's constants, fua_dg_skyportal_scale).
	"    vec2 uv = gl_FragCoord.xy / vec2(textureSize(sampler2D(uSky, uSky_sampler), 0));\n"
	"    outColor = vec4(texture(sampler2D(uSky, uSky_sampler), uv).rgb, 1.0);\n"
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
	// [rc4l] uSky's sampler, uSky_sampler, is immutable, or it is a variable nothing binds.
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
	// [rc4l] Depth tested and written, which measured cheaper at all three vantages tried than leaving the passes after it to draw over the sky.
	pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = true;
	// [rc4l] LESS_EQUAL as GL's GL_LEQUAL, since a sky surface is coplanar with the wall it replaces and fails LESS every time while looking correct from outside.
	pci.GraphicsPipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
	// [rc4l] No depth bias, since a surface coplanar with the wall it replaces would lose every such contest, and GL's own quads need none.
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
	CreatePipeline(pci, &pso);
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

	// [rc4l] Full resolution, since a sky portal is a view of a city whose pixels land 1:1 and halved looked softer than GL, with fua_dg_skyportal_scale left to halve it when a map needs the frame time.
	int div = fua_dg_skyportal_scale;
	if (div < 1) div = 1;
	if (div > 4) div = 4;
	const int w = FrameWidth() / div, h = FrameHeight() / div;
	if (w <= 0 || h <= 0) return false;
	if (!g_skyPortalColor || g_skyPortalW != w || g_skyPortalH != h)
	{
		g_skyPortalColor.Release();
		g_skyPortalDepth.Release();
		g_skyPortalNormal.Release();
		g_skyPortalSRB.Release();
		g_skySurfPSO.Release();
		g_skySurfSRB.Release();
		if (!CreateViewTextures(w, h, "fua sky portal", g_skyPortalColor, g_skyPortalDepth)) return false;
		CreateNormalTarget(w, h, "fua sky portal", g_skyPortalNormal);
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

// [rc4l] The world from the skybox viewpoint, rendered before the frame's own world pass, since the world draw derives its visible set once per call and re-entering it loses the map.
bool RenderSkyPortal(Diligent::IDeviceContext *ctx)
{
	g_skyPortalActive = false;
	if (!fua_dg_skyportal) return false;
	const zx::gamecopy::ActorRecord *sb = CurrentSkyViewpoint();
	if (sb == NULL) return false;
	if (!EnsureSkyPortalResources()) return false;

	auto *brtv = FrameRTV();
	// [rc4l] The world pass's own readable depth buffer, since the frame's is one nothing has written to.
	Diligent::ITextureView *bdsv = EnsureSceneDepth();
	if (!bdsv) bdsv = FrameDSV();
	auto *prtv = g_skyPortalColor->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
	auto *pdsv = g_skyPortalDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
	if (!prtv || !pdsv) return false;

	const fixed_t sx = viewx, sy = viewy, sz = viewz;
	const angle_t sa = viewangle;
	float savedMVP[16];
	for (int i = 0; i < 16; i++) savedMVP[i] = g_mvp[i];

	// [rc4l] The viewpoint interpolated with its own angle added to the camera's, which is how a skybox parallaxes and turns, and only when it has a previous position, since one that never moved lerps from the map's origin.
	const fixed_t frac = r_TicFrac;
	const bool moved = (sb->PrevX != 0 || sb->PrevY != 0 || sb->PrevZ != 0);
	viewx = moved ? sb->PrevX + FixedMul(frac, sb->x - sb->PrevX) : sb->x;
	viewy = moved ? sb->PrevY + FixedMul(frac, sb->y - sb->PrevY) : sb->y;
	viewz = moved ? sb->PrevZ + FixedMul(frac, sb->z - sb->PrevZ) : sb->z;
	viewangle = sa + (moved ? zx::InterpolateAngleBAM(sb->PrevAngle, sb->angle, (int64_t)frac)
	                        : sb->angle);

	// [rc4l] The eye never sits inside the skybox room's floor or ceiling.
	if (sb->Sector != NULL)
	{
		const fixed_t fl = sb->Sector->rc.floorplane.ZatPoint(sb->x, sb->y);
		const fixed_t ce = sb->Sector->rc.ceilingplane.ZatPoint(sb->x, sb->y);
		if (viewz < fl + 4 * FRACUNIT) viewz = fl + 4 * FRACUNIT;
		if (viewz > ce - 4 * FRACUNIT) viewz = ce - 4 * FRACUNIT;
	}

	// [rc4l] Where the portal camera stands, recorded before the restore below hands back the player's position, which made the reach test exclude everything.
	g_pass.skyCamX = FIXED2FLOAT(viewx);
	g_pass.skyCamY = FIXED2FLOAT(viewy);
	g_pass.skyCamZ = FIXED2FLOAT(viewz);
	BuildMVP(g_mvp);
	viewx = sx; viewy = sy; viewz = sz; viewangle = sa;

	const float clearCol[4] = { 0.f, 0.f, 0.f, 1.f };
	BeginPassTargets(ctx, ViewPassTargets(g_skyPortalColor, g_skyPortalDepth, g_skyPortalNormal, true), clearCol);
	if (g_cb)
	{
		Diligent::MapHelper<float> cb(ctx, g_cb, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		for (int k = 0; k < 16; k++) cb[k] = g_mvp[k];
	}
	g_pass.inSkyPortal = true;
	DrawWorld(ctx);
	g_pass.inSkyPortal = false;

	// [rc4l] The screen gets back its own target and full viewport, which the frame assumes.
	for (int k = 0; k < 16; k++) g_mvp[k] = savedMVP[k];
	ctx->SetRenderTargets(1, &brtv, bdsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->SetViewports(1, nullptr, 0, 0);
	if (g_cb)
	{
		Diligent::MapHelper<float> cb(ctx, g_cb, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		for (int k = 0; k < 16; k++) cb[k] = g_mvp[k];
	}
	// [rc4l] The picture changes hands here, outside the frame's render pass, since a transition asked for mid-pass ends the pass and the world drawn after lands outside it.
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

// [rc4l] What the viewpoint saw, laid down as the background with depth left open, so the world draws over it in the same pass.
bool BlitSkyPortal(Diligent::IDeviceContext *ctx)
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
	// [rc4l] Sized once and filled, as a Push a vertex was 3% of a Sunder MAP14 frame's main thread.
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

void DrawSkyOccluders(Diligent::IDeviceContext *ctx)
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

// [rc4l] The sky surfaces painted from the portal's picture when this frame's sky is one; false when none drew.
bool DrawSkyPortalSurfaces(Diligent::IDeviceContext *ctx)
{
	if (!g_skyPortalActive || !g_skySurfPSO || !g_skySurfSRB) return false;

	// [rc4l] Its own buffer, rebuilt each frame, since GL clears the shared dynamic stream when it re-enters its scene builder for the skybox portal, between the walk that produces these and the draw.
	static TArray<SceneVertex> sv;
	GatherSkySurfaces(sv);
	if (!BindScreenSurfaces(ctx, g_skySurfVB, g_skySurfVBCapacity, sv, g_skySurfPSO, "fua sky surface VB")) return false;
	DrawScreenSurfaceRun(ctx, g_skySurfSRB, 0, sv.Size());
	return true;
}

bool SkyPortalActive() { return g_skyPortalActive; }

void ReleaseSkyPortal()
{
	g_skyPortalSRB.Release();
	g_skyPortalPSO.Release();
	g_skyPortalColor.Release();
	g_skyPortalDepth.Release();
	g_skyPortalNormal.Release();
	g_skyPortalW = g_skyPortalH = 0;
	g_skyOccluderSRB.Release();
	g_skyOccluderPSO.Release();
}

// [rc4l] The pipeline that paints one picture over a whole target, which the stacked-view display borrows.
Diligent::IPipelineState *SkyPortalPicturePSO() { return g_skyPortalPSO; }

}} // namespace zx::hwrender
