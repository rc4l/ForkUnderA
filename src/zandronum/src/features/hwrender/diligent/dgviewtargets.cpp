// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Views drawn into pooled targets at the window's size and painted onto surfaces from them: reflective floors and ceilings, and stacked sectors.

#include "features/hwrender/diligent/dgscene_internal.h"

namespace zx { namespace hwrender {

// [rc4l] The pipeline that paints a stacked plane from its view.
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_stackSurfPSO;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_stackSurfVB;
static unsigned int g_stackSurfVBCapacity = 0;

// [rc4l] The world from another eye, at the window's size, and the binding that paints a surface from it: a reflective plane's reflection, a stacked view.
struct ViewTarget
{
	Diligent::RefCntAutoPtr<Diligent::ITexture> color;
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
	int w, h;
	ViewTarget() : w(0), h(0) {}
	void Release() { srb.Release(); color.Release(); w = h = 0; }
};

// [rc4l] The depth every view target is rendered with, one view at a time, since a view's picture is all that outlives its render.
static Diligent::RefCntAutoPtr<Diligent::ITexture> g_viewDepth;
// [rc4l] And the normals their G-buffer writes and their decal pass reads, at the frame's size like every view but a camera's.
static Diligent::RefCntAutoPtr<Diligent::ITexture> g_viewNormal;

Diligent::ITexture *FrameViewNormal()
{
	const int w = FrameWidth(), h = FrameHeight();
	if (g_viewNormal && (int)g_viewNormal->GetDesc().Width == w && (int)g_viewNormal->GetDesc().Height == h) return g_viewNormal;
	if (!CreateNormalTarget(w, h, "fua view", g_viewNormal)) return NULL;
	return g_viewNormal;
}

// [rc4l] Makes a target's picture at the window's size, and its binding on pso's texture variable, whenever it has none at that size.
static bool EnsureViewTarget(ViewTarget &t, Diligent::IPipelineState *pso, const char *var, const char *name)
{
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap || !pso) return false;
	const int w = FrameWidth(), h = FrameHeight();
	if (t.color && t.srb && t.w == w && t.h == h) return true;
	t.Release();
	if (!CreateColorTarget(w, h, name, t.color) ||
		!CreatePictureBinding(pso, var, t.color, t.srb)) return false;
	t.w = w; t.h = h;
	return true;
}

// [rc4l] Every target of a pool made at the window's size at once, the pool `size` long, so no view in sight waits on one being made; false when one cannot be.
static bool FitViewPool(TArray<ViewTarget> &pool, unsigned size, Diligent::IPipelineState *pso, const char *var, const char *name)
{
	if (pool.Size() != size) pool.Resize(size);
	for (unsigned k = 0; k < size; k++)
		if (!EnsureViewTarget(pool[k], pso, var, name)) return false;
	return true;
}

// [rc4l] The shared view depth at a target's size, made when it has none at that size.
static Diligent::ITexture *ViewDepth(const ViewTarget &t)
{
	if (g_viewDepth)
	{
		const auto &d = g_viewDepth->GetDesc();
		if ((int)d.Width == t.w && (int)d.Height == t.h) return g_viewDepth;
	}
	if (!CreateDepthTarget(t.w, t.h, "fua view", g_viewDepth)) return NULL;
	return g_viewDepth;
}

// [rc4l] The world into a target, from the eye now in viewx/viewy/viewz/viewpitch and through g_pass.clipPlane.
static void RenderWorldInto(Diligent::IDeviceContext *ctx, ViewTarget &t)
{
	Diligent::ITexture *depth = ViewDepth(t);
	if (depth == NULL) return;
	BuildMVP(g_mvp);
	// [rc4l] A stacked view's other skies from its own walk's, a plane mirror's from the player's and the mirrors'.
	RenderViewSkyLooks(ctx, false, g_pass.stackSlot, g_pass.stackSlot < 0, t.w, t.h);
	const float clear[4] = { 0.05f, 0.06f, 0.09f, 1.0f };
	BeginPassTargets(ctx, ViewPassTargets(t.color, depth, FrameViewNormal(), true), clear);
	DrawWorld(ctx);
	ForgetViewSkyLooks();
}

// [rc4l] A rendered target changes hands here, outside the frame's render pass, for the reason RenderSkyPortal gives.
static void HandOverViewTarget(Diligent::IDeviceContext *ctx, ViewTarget &t)
{
	Diligent::StateTransitionDesc barrier(t.color, Diligent::RESOURCE_STATE_UNKNOWN,
		Diligent::RESOURCE_STATE_SHADER_RESOURCE, Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE);
	ctx->TransitionResourceStates(1, &barrier);
}

// [rc4l] Reflective floors and ceilings as GL's GLPlaneMirrorPortal draws them: each visible plane gets the world rendered from the mirrored eye into a slot before the frame, and the world pass paints its pieces from that picture, flipped, under the translucent floor (decisions in planemirror_compute).
// [rc4l] One target per distinct reflective plane the level has now, as GL has no limit, all made before any comes into view.
static TArray<ViewTarget> g_pm;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_pmPSO;
// [rc4l] Reflections rendered this frame.
static int g_pmUsed = 0;

void ReleasePlaneMirrors()
{
	g_pm.Clear();
	g_pmPSO.Release();
	g_viewDepth.Release();
	g_viewNormal.Release();
	g_pmUsed = 0;
}

// [rc4l] The plane-mirror pipeline, and a target for each of the level's `planes` reflective planes.
static bool EnsurePlaneMirrorResources(int planes)
{
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap || !g_cb) return false;
	const auto &sd = swap->GetDesc();
	if (!g_pmPSO)
	{
		static const char *kPlaneMirrorVS =
			"#version 450\n"
			"layout(location = 0) in vec3 aPos;\n"
			"layout(location = 1) in vec2 aUV;\n"
			"layout(location = 2) in float aPiece;\n"
			FUA_CONSTANTS_GLSL
			"void main() { gl_Position = uMVP * vec4(aPos, 1.0); }\n";
		// [rc4l] The mirrored eye sees the reflection upright, so the plane samples it upside down at its own screen position.
		static const char *kPlaneMirrorPS =
			"#version 450\n"
			"layout(binding = 1) uniform texture2D uReflection; uniform sampler uReflection_sampler;\n"
			"layout(location = 0) out vec4 outColor;\n"
			"void main() {\n"
			"    ivec2 size = textureSize(sampler2D(uReflection, uReflection_sampler), 0);\n"
			"    ivec2 p = ivec2(gl_FragCoord.xy);\n"
			"    outColor = vec4(texelFetch(sampler2D(uReflection, uReflection_sampler), ivec2(p.x, size.y - 1 - p.y), 0).rgb, 1.0);\n"
			"}\n";
		Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
		vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua plane mirror VS", kPlaneMirrorVS);
		ps = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua plane mirror PS", kPlaneMirrorPS);
		if (!vs || !ps) return false;
		static Diligent::ShaderResourceVariableDesc vars[] = {
			{ Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ Diligent::SHADER_TYPE_PIXEL, "uReflection", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		};
		Diligent::LayoutElement elems[] = {
			Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
			Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false},
			Diligent::LayoutElement{2, 0, 1, Diligent::VT_FLOAT32, false},
		};
		Diligent::GraphicsPipelineStateCreateInfo pci;
		pci.PSODesc.Name = "fua plane mirror PSO";
		pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
		ClampDepthLikeGL(pci.GraphicsPipeline);
		pci.GraphicsPipeline.NumRenderTargets = 1;
		pci.GraphicsPipeline.RTVFormats[0] = sd.ColorBufferFormat;
		pci.GraphicsPipeline.DSVFormat = sd.DepthBufferFormat;
		pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
		// [rc4l] Tested so nearer walls hide it, never written, so the translucent floor drawn over it still passes on the same plane.
		pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
		pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
		pci.GraphicsPipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
		pci.GraphicsPipeline.InputLayout.LayoutElements = elems;
		pci.GraphicsPipeline.InputLayout.NumElements = 3;
		pci.PSODesc.ResourceLayout.Variables = vars;
		pci.PSODesc.ResourceLayout.NumVariables = 2;
		// [rc4l] A combined image sampler needs a sampler in its descriptor even though texelFetch never uses it; without one the driver faults writing the binding.
		Diligent::SamplerDesc samp;
		samp.MinFilter = samp.MagFilter = Diligent::FILTER_TYPE_POINT;
		samp.MipFilter = Diligent::FILTER_TYPE_POINT;
		samp.AddressU = samp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
		Diligent::ImmutableSamplerDesc samplers[] = { { Diligent::SHADER_TYPE_PIXEL, "uReflection", samp } };
		pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
		pci.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
		pci.pVS = vs;
		pci.pPS = ps;
		CreatePipeline(pci, &g_pmPSO);
		if (!g_pmPSO) return false;
		BindSceneConstants(g_pmPSO);
	}
	return FitViewPool(g_pm, (unsigned)planes, g_pmPSO, "uReflection", "fua plane mirror");
}

// [rc4l] Before the frame: pick the reflective planes the eye faces and can see, and render each one's reflection.
void RenderPlaneMirrors(Diligent::IDeviceContext *ctx)
{
	g_pmUsed = 0;
	for (unsigned i = 0; i < g_scene.blendBatches.Size(); i++) g_scene.batches[g_scene.blendBatches[i]].mirrorSlot = -1;
	if (!fua_dg_planemirrors || sectors == NULL || !g_scene.vb) return;

	float mvp[16];
	BuildMVP(mvp);
	const float vz = FIXED2FLOAT(viewz);
	// [rc4l] Every reflective plane the level has now, and of those the ones the eye faces and can see, each the slot it is rendered into.
	static TArray<zx::hwrender::MirrorPlane> planes, slots;
	planes.Resize(g_scene.blendBatches.Size());
	slots.Resize(g_scene.blendBatches.Size());
	int planeCount = 0;
	const int maxSlots = (int)planes.Size();
	for (unsigned i = 0; i < g_scene.blendBatches.Size(); i++)
	{
		SceneBatch &b = g_scene.batches[g_scene.blendBatches[i]];
		if (b.mirrorSector < 0 || b.mirrorSector >= numsectors || b.count == 0) continue;
		const sector_t &s = sectors[b.mirrorSector];
		zx::hwrender::MirrorPlane p;
		p.ceiling = b.mirrorCeiling;
		p.z = FIXED2FLOAT((b.mirrorCeiling ? s.rc.ceilingplane : s.rc.floorplane).ZatPoint(viewx, viewy));
		zx::hwrender::ComputePlaneMirrorSlot(&planes[0], planeCount, maxSlots, p);
		if (!zx::hwrender::ComputePlaneMirrorFaces(b.mirrorCeiling, p.z, vz)) continue;
		const float mn[3] = { b.minX, b.minY, b.minZ }, mx[3] = { b.maxX, b.maxY, b.maxZ };
		if (!zx::hwrender::ComputeBoxMayBeVisible(mvp, mn, mx)) continue;
		b.mirrorSlot = zx::hwrender::ComputePlaneMirrorSlot(&slots[0], g_pmUsed, maxSlots, p);
	}
	if (planeCount == 0) return;
	if (!EnsurePlaneMirrorResources(planeCount)) { g_pmUsed = 0; return; }
	if (g_pmUsed == 0) return;

	const fixed_t sz = viewz;
	const int sp = viewpitch;
	float savedMVP[16];
	for (int k = 0; k < 16; k++) savedMVP[k] = g_mvp[k];
	for (int k = 0; k < g_pmUsed; k++)
	{
		viewz = FLOAT2FIXED(zx::hwrender::ComputePlaneMirrorEyeZ(slots[k].z, vz));
		viewpitch = -sp;
		zx::hwrender::ComputePlaneMirrorClip(slots[k].ceiling, slots[k].z, g_pass.clipPlane);
		RenderWorldInto(ctx, g_pm[k]);
	}
	viewz = sz;
	viewpitch = sp;
	for (int k = 0; k < 16; k++) g_mvp[k] = savedMVP[k];
	g_pass.clipPlane[0] = g_pass.clipPlane[1] = g_pass.clipPlane[2] = g_pass.clipPlane[3] = 0.f;
	for (int k = 0; k < g_pmUsed; k++) HandOverViewTarget(ctx, g_pm[k]);
}

// [rc4l] In the player's view, before the translucent pass: each reflective piece painted from its plane's reflection.
void DrawPlaneMirrorSurfaces(Diligent::IDeviceContext *ctx)
{
	if (!g_pass.mainView || g_pmUsed == 0 || !g_pmPSO || !g_scene.vb) return;
	Diligent::IBuffer *vbs[] = { g_scene.vb };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	ctx->SetPipelineState(g_pmPSO);
	int bound = -1;
	for (unsigned i = 0; i < g_scene.blendBatches.Size(); i++)
	{
		const SceneBatch &b = g_scene.batches[g_scene.blendBatches[i]];
		if (b.mirrorSlot < 0 || b.mirrorSlot >= g_pmUsed || b.mirrorSlot >= (int)g_pm.Size() || !g_pm[b.mirrorSlot].srb) continue;
		if (b.mirrorSlot != bound)
		{
			ctx->CommitShaderResources(g_pm[b.mirrorSlot].srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
			bound = b.mirrorSlot;
		}
		Diligent::DrawAttribs d;
		d.NumVertices = b.count;
		d.StartVertexLocation = b.first;
		ctx->Draw(d);
	}
}

// [rc4l] Stacked sectors as GL's GLSectorStackPortal draws them: each view the player's walk saw is the world from the eye moved by its portal, clipped at its planes, and the planes are painted from that picture.
// [rc4l] One target per stacked view the level can show (StackViewCapacity), all made before any comes into view.
static TArray<ViewTarget> g_stack;
static int g_stackDrawn = 0;   // [rc4l] Views rendered this frame.
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_stackShowSRB;   // [rc4l] fua_dg_stackshow's blit

void ReleaseStackPortals()
{
	g_stack.Clear();
	g_viewDepth.Release();
	g_viewNormal.Release();
	g_stackShowSRB.Release();
	g_stackSurfPSO.Release();
	g_stackDrawn = 0;
	g_scene.stackVisReady = false;
}

// [rc4l] What GL's walks drew this frame and the player's sky holes, uploaded before any pass, outside the frame's render pass, since the stacked views and the main pass both read them.
void UploadWalkVisibility(Diligent::IDeviceContext *ctx)
{
	int visWords = 0;
	const unsigned int *visBits = zx::levelmesh::StackVisibility(visWords);
	g_scene.stackVisReady = visBits != NULL && g_scene.stackVisBuf && (unsigned int)visWords <= g_scene.stackVisCapacity;
	if (g_scene.stackVisReady)
		UpdateBufferInPages(ctx, g_scene.stackVisBuf, 0, (size_t)visWords * sizeof(unsigned int), visBits);
	int holes = 0;
	const float *hole = zx::levelmesh::SkyHoles(holes);
	g_scene.skyHoleCount = (g_scene.stackVisReady && hole != NULL && g_scene.skyHoleBuf) ? holes : 0;
	if (g_scene.skyHoleCount > 0)
		UpdateBufferInPages(ctx, g_scene.skyHoleBuf, 0, (size_t)g_scene.skyHoleCount * zx::levelmesh::kSkyHoleFloats * sizeof(float), hole);
}

// [rc4l] Before the frame and after the sky's picture, which each view paints behind its own world.
void RenderStackPortals(Diligent::IDeviceContext *ctx)
{
	g_stackDrawn = 0;
	const int capacity = zx::levelmesh::StackViewCapacity();
	if (!fua_dg_stackportals || capacity <= 0 || !g_scene.vb || !g_cb) return;
	if (!g_stackSurfPSO)
		g_stackSurfPSO = CreateScreenSurfacePSO("fua stack surface", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);
	if (!g_stackSurfPSO) return;
	if (!FitViewPool(g_stack, (unsigned)capacity, g_stackSurfPSO, "uSky", "fua stacked view")) return;
	const int n = zx::levelmesh::StackViewCount() < capacity ? zx::levelmesh::StackViewCount() : capacity;
	if (n <= 0) return;

	const fixed_t sx = viewx, sy = viewy;
	float savedMVP[16];
	for (int k = 0; k < 16; k++) savedMVP[k] = g_mvp[k];
	for (int k = 0; k < n; k++)
	{
		const zx::levelmesh::StackViewInfo v = zx::levelmesh::StackView(k);
		// [rc4l] No clip plane: GL's stacked walk clips nothing, and the cull already keeps to what it drew.
		viewx = sx + FLOAT2FIXED(v.dx);
		viewy = sy + FLOAT2FIXED(v.dy);
		g_pass.stackSlot = k;
		RenderWorldInto(ctx, g_stack[k]);
		g_stackDrawn = k + 1;
	}
	g_pass.stackSlot = -1;
	viewx = sx; viewy = sy;
	for (int k = 0; k < 16; k++) g_mvp[k] = savedMVP[k];
	for (int k = 0; k < g_stackDrawn; k++) HandOverViewTarget(ctx, g_stack[k]);
}

// [rc4l] What the stacked views did last frame, for fua_dg_stackinfo: views drawn, whether the cull had its bits, and how many subsectors each view's walk reached.
void StackInfo(FString &out)
{
	int words = 0;
	const unsigned int *bits = zx::levelmesh::StackVisibility(words);
	const int slots = zx::levelmesh::WalkSlots();
	TArray<int> reached;
	reached.Resize(slots);
	for (int s = 0; s < slots; s++) reached[s] = zx::levelmesh::ComputeSlotCount(bits, words, numsubsectors + numsides, s, slots);
	const int view0 = zx::levelmesh::ComputeStackWalkSlot(0), view1 = zx::levelmesh::ComputeStackWalkSlot(1);
	out.Format("stacked views: %d drawn of %d the level can show, cull %s, %d words; subsectors and sidedefs drawn by view 0: %d, view 1: %d, "
		"the player's walk: %d, the skybox walk: %d (of %d); %d sky holes; plane mirrors: %d rendered of %d planes",
		g_stackDrawn, zx::levelmesh::StackViewCapacity(), g_scene.stackVisReady ? "on" : "off", words,
		view0 < slots ? reached[view0] : 0, view1 < slots ? reached[view1] : 0, reached[zx::levelmesh::kMainWalkSlot],
		reached[zx::levelmesh::kSkyboxWalkSlot], numsubsectors + numsides, g_scene.skyHoleCount, g_pmUsed, (int)g_pm.Size());
}

// [rc4l] In the player's view, with the sky: each stacked plane painted from its view's picture, and depth-written so nothing behind it shows through.
void DrawStackSurfaces(Diligent::IDeviceContext *ctx)
{
	if (!g_pass.mainView || g_stackDrawn == 0 || !g_stackSurfPSO) return;
	static TArray<SceneVertex> sv;
	static TArray<unsigned int> first, count;
	first.Resize(g_stackDrawn);
	count.Resize(g_stackDrawn);
	sv.Clear();
	for (int k = 0; k < g_stackDrawn; k++)
	{
		int n = 0;
		const zx::levelmesh::MeshVertex *src = zx::levelmesh::StackViewVertices(k, n);
		first[k] = sv.Size();
		for (int i = 0; i < n; i++) sv.Push(ToSceneVertex(src[i], 0));
		// [rc4l] And the cap GL closes the stack's stencil with, so every unblocked ray past the plane sees the view, not only those through it.
		const bool ceiling = zx::levelmesh::StackView(k).ceiling;
		for (int c = 0; c < 6; c++)
		{
			float x, y, z;
			zx::levelmesh::ComputeStackCapCorner(ceiling, c, x, y, z);
			zx::levelmesh::MeshVertex fv;
			fv.Set(x, z, y, 0.f, 0.f);
			sv.Push(ToSceneVertex(fv, 0));
		}
		count[k] = sv.Size() - first[k];
	}
	if (!BindScreenSurfaces(ctx, g_stackSurfVB, g_stackSurfVBCapacity, sv, g_stackSurfPSO, "fua stack surface VB")) return;
	for (int k = 0; k < g_stackDrawn; k++)
		if (count[k] != 0 && g_stack[k].srb) DrawScreenSurfaceRun(ctx, g_stack[k].srb, first[k], count[k]);
}

// [rc4l] fua_dg_stackshow: the first stacked view's picture over the frame, through the sky portal's full-screen blit.
void ShowStackView(Diligent::IDeviceContext *ctx)
{
	if (!fua_dg_stackshow || g_stackDrawn == 0 || !SkyPortalPicturePSO() || !g_stack[0].color) return;
	if (!g_stackShowSRB) SkyPortalPicturePSO()->CreateShaderResourceBinding(&g_stackShowSRB, true);
	if (!g_stackShowSRB) return;
	if (auto *v = g_stackShowSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex"))
		v->Set(g_stack[0].color->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
	ctx->SetPipelineState(SkyPortalPicturePSO());
	ctx->CommitShaderResources(g_stackShowSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
	Diligent::DrawAttribs dr;
	dr.NumVertices = 3;
	ctx->Draw(dr);
}

}} // namespace zx::hwrender
