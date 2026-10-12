// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Camera textures: the world drawn from a camera's viewpoint into the texture a material samples.

#include "features/hwrender/diligent/dgscene_internal.h"

namespace zx { namespace hwrender {

// [rc4l] Camera textures: the world, rendered from somewhere else, into a texture.
//
// A camera texture has no pixels of its own -- the engine renders into it every frame -- so reading
// its bytes returns whatever the buffer happens to hold. That was the red/blue noise on gvh06's
// teleporter and the flat slab on gvh07's monitor: the same bug wearing two faces.
//
// Nothing here is specific to cameras. It is the world drawn with a different view matrix into a
// different target, which is also what a portal, a mirror and a skybox each need, so this is the
// seam those get built on rather than a one-off.
struct CameraTarget
{
	const void *material;
	int w, h;
	Diligent::RefCntAutoPtr<Diligent::ITexture> color, depth;
	Diligent::ITextureView *rtv, *dsv, *srv;
	// [rc4l] Where the world pass draws, top row first, before it is copied into color the way GL keeps it.
	Diligent::RefCntAutoPtr<Diligent::ITexture> scratch;
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> flipSRB;
};
static TArray<CameraTarget *> g_cameraTargets;

// [rc4l] GL keeps a camera texture bottom row first (glCopyTexSubImage), and every coordinate that samples one, a wall's, a flat's or the HUD's, is GL's, so the camera's picture is copied in upside down; kSkyPortalVS hands this an upright vUV.
static const char *kCameraFlipPS =
	"#version 450\n"
	"layout(location = 0) in vec2 vUV;\n"
	"layout(binding = 1) uniform sampler2D uTex;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() { outColor = vec4(texture(uTex, vec2(vUV.x, 1.0 - vUV.y)).rgb, 1.0); }\n";
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_cameraFlipPSO;

// [rc4l] The scratch picture into the camera texture upside down, outside any pass that samples it.
static void CopyCameraFlipped(Diligent::IDeviceContext *ctx, CameraTarget &cam)
{
	// [rc4l] Point sampled, since the copy is texel for texel, and with no depth buffer bound.
	if (!g_cameraFlipPSO && !CreatePicturePSO("fua camera flip", kCameraFlipPS, Diligent::FILTER_TYPE_POINT, Diligent::TEX_FORMAT_UNKNOWN, g_cameraFlipPSO))
		return;
	if (!cam.flipSRB && !CreatePictureBinding(g_cameraFlipPSO, "uTex", cam.scratch, cam.flipSRB)) return;
	Diligent::StateTransitionDesc barrier(cam.scratch, Diligent::RESOURCE_STATE_UNKNOWN,
		Diligent::RESOURCE_STATE_SHADER_RESOURCE, Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE);
	ctx->TransitionResourceStates(1, &barrier);
	Diligent::ITextureView *rtv = cam.rtv;
	ctx->SetRenderTargets(1, &rtv, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->SetPipelineState(g_cameraFlipPSO);
	ctx->CommitShaderResources(cam.flipSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
	Diligent::DrawAttribs dr;
	dr.NumVertices = 3;
	ctx->Draw(dr);
}

static CameraTarget *GetCameraTarget(const void *material, int w, int h)
{
	for (unsigned i = 0; i < g_cameraTargets.Size(); i++)
	{
		CameraTarget *c = g_cameraTargets[i];
		if (c->material == material && c->w == w && c->h == h) return c;
	}
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap || w <= 0 || h <= 0) return NULL;

	CameraTarget *c = new CameraTarget();
	c->material = material; c->w = w; c->h = h;
	c->rtv = c->dsv = c->srv = NULL;

	if (!CreateViewTextures(w, h, "fua camera", c->scratch, c->depth) || !CreateColorTarget(w, h, "fua camera flipped", c->color))
	{ delete c; return NULL; }
	c->rtv = c->color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
	c->srv = c->color->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
	c->dsv = c->depth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
	g_cameraTargets.Push(c);
	// [rc4l] Its material may already sit in the bindless array as white, filled before this first render, and the retries for that may be spent.
	DirtyMaterialSlots();
	return c;
}

// The rendered view for a canvas material, or NULL if it has never been rendered.
Diligent::ITextureView *GetCameraSRV(const void *material)
{
	for (unsigned i = 0; i < g_cameraTargets.Size(); i++)
		if (g_cameraTargets[i]->material == material) return g_cameraTargets[i]->srv;
	return NULL;
}

// [rc4l] Render one camera texture. Called from FGLInterface::RenderTextureView, once per visible
// camera per frame, with the viewpoint the engine picked.
//
// The viewpoint arrives by temporarily standing the global camera somewhere else. BuildMVP reads
// viewx/viewy/viewz/viewangle/viewpitch because every other caller wants the player's view; swapping
// them around this call is smaller and less error-prone than threading a viewpoint through the
// matrix builder and every one of its callers, and they are restored before anything else can look.
void RenderCameraTexture(const void *material, int w, int h, float aspect,
                         fixed_t px, fixed_t py, fixed_t pz, unsigned int pangle, int ppitch, float fovDeg)
{
	if (!GetDevice() || !g_scene.vb || !g_pso.opaque) return;
	CameraTarget *cam = GetCameraTarget(material, w, h);
	if (!cam) return;

	auto *ctx = GetContext();

	const fixed_t sx = viewx, sy = viewy, sz = viewz;
	const angle_t sa = viewangle;
	const int sp = viewpitch;
	viewx = px; viewy = py; viewz = pz; viewangle = pangle; viewpitch = ppitch;
	g_frame.aspectOverride = (aspect > 0.f) ? aspect : 1.f;
	// [rc4l] The camera's FOV is across, as GL's SetProjection takes it, and its height follows from the picture's shape: taken as the height, a wide TV saw a smaller moon lower in the sky.
	g_frame.fovOverride = ::hwrender::ComputeSceneFovY(fovDeg, g_frame.aspectOverride) * 3.14159265f / 180.0f;
	BuildMVP(g_mvp);
	g_frame.fovOverride = g_frame.aspectOverride = 0.f;
	viewx = sx; viewy = sy; viewz = sz; viewangle = sa; viewpitch = sp;

	Diligent::ITextureView *rtv = cam->scratch->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
	ctx->SetRenderTargets(1, &rtv, cam->dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	Diligent::Viewport vp;
	vp.TopLeftX = 0; vp.TopLeftY = 0;
	vp.Width = (float)w; vp.Height = (float)h;
	vp.MinDepth = 0.f; vp.MaxDepth = 1.f;
	ctx->SetViewports(1, &vp, (Diligent::Uint32)w, (Diligent::Uint32)h);
	const float clear[4] = { 0.05f, 0.06f, 0.09f, 1.0f };
	ctx->ClearRenderTarget(rtv, clear, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->ClearDepthStencil(cam->dsv, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0,
		Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	// [rc4l] GL renders a camera texture during its own frame, before the backend's, so the frame-wide jobs run for it as they always have.
	g_frame.serial++;
	g_pass.inCameraPass = true;
	DrawWorld(ctx);
	g_pass.inCameraPass = false;
	CopyCameraFlipped(ctx, *cam);

	// Hand the screen back its own target and full viewport; the main pass assumes both.
	auto *swap = GetSwapChain();
	auto *brtv = swap->GetCurrentBackBufferRTV();
	// [rc4l] The world pass renders into our own readable depth buffer, so restoring the swapchain's
	// here would hand the screen a depth buffer nothing has written to.
	Diligent::ITextureView *bdsv = EnsureSceneDepth();
	if (!bdsv) bdsv = swap->GetDepthBufferDSV();
	ctx->SetRenderTargets(1, &brtv, bdsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->SetViewports(1, nullptr, 0, 0);
	BuildMVP(g_mvp);
}

void ReleaseCameraTargets()
{
	for (unsigned i = 0; i < g_cameraTargets.Size(); i++) delete g_cameraTargets[i];
	g_cameraTargets.Clear();
}

}} // namespace zx::hwrender
