// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The 2D pass: the status bar, weapon, messages, menus and console, drawn over the world from the quads hud2d captured.


#include "MapHelper.hpp"
#include "RefCntAutoPtr.hpp"

#include "features/hwrender/diligent/dgshared.h"
#include "features/hwrender/hud2d.h"
#include "features/hwrender/computation/sceneconstants_compute.h"   // [rc4l] ComputeFixedColormapConstants
#include "features/hwrender/computation/warp_compute.h"   // [rc4l] ComputeWarpTimer, the fuzz clock
#include "features/rendersettings/rendersettings.h"   // [rc4l] gl_frameMS, the frame's clock
#include "features/viewsetup/viewsetup.h"   // [rc4l] FixedColormapShadeFor
#include "v_video.h"
#include "tarray.h"
#include "c_cvars.h"

EXTERN_CVAR(Bool, fua_dg_hud)

namespace zx { namespace hwrender {

// ---------------------------------------------------------------------------
// 2D / HUD
// ---------------------------------------------------------------------------
//
// [rc4l] The status bar, weapon, messages, menus and console, from the quad list features/hwrender
// captured off the engine's own 2D path (see hud2d.h). Its own pipeline because 2D is a different
// problem from 3D: orthographic, no depth at all, painter's order, and blended throughout.
//
// Submission order is draw order and must not be reordered -- no material batching here. A 2D layer
// sorted by texture would put the status bar behind the world and the console behind the menu.
// [rc4l] fixedCm is 1 for a weapon layer, which takes the view's fixed colormap, and fuzz its fuzz shader (fuzz_compute's kFuzz*).
struct Vertex2D { float x, y, u, v, r, g, b, a, texMode, fixedCm, fuzz, tint[4], tintRange[3]; };

// [rc4l] The 2D constant block both stages read: the screen's reciprocal size and the fuzz clock in seconds (z), then the weapon's fixed colormap as the scene block carries it.
#define FUA_SCREEN2D_GLSL "layout(binding = 0) uniform Screen { vec4 uInvScreen; vec4 uFixedColormap; vec4 uFixedRange; };\n"

// [rc4l] One 2D pipeline per blend mode: 0 alpha, 1 additive, 2 multiply.
//
// There used to be one, always alpha, and Quad2D::blend was recorded and then ignored -- so additive
// 2D drew as ordinary alpha. Multiply is what a fixed colormap needs (invulnerability tints the
// screen by multiplying it), and neither can be faked with the alpha state.
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_pso2D[3];
// [rc4l] A polygon's triangles, alpha-blended with the texture repeating, as FillSimplePoly samples a flat.
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_pso2DWrap;
static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_vb2D;
static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_cb2D;
static unsigned int g_vb2DCapacity = 0;
static int g_draws2D = 0, g_quads2D = 0;

static const char *k2DVS =
	"#version 450\n"
	"layout(location = 0) in vec2 aPos;\n"
	"layout(location = 1) in vec2 aUV;\n"
	"layout(location = 2) in vec4 aColor;\n"
	"layout(location = 3) in float aTexMode;\n"
	"layout(location = 4) in float aFixed;\n"
	"layout(location = 5) in float aFuzz;\n"
	"layout(location = 6) in vec4 aTint;\n"
	"layout(location = 7) in vec3 aTintRange;\n"
	FUA_SCREEN2D_GLSL
	"layout(location = 0) out vec2 vUV;\n"
	"layout(location = 1) out vec4 vColor;\n"
	"layout(location = 2) flat out int vTexMode;\n"
	"layout(location = 3) flat out float vFixed;\n"
	"layout(location = 4) flat out float vFuzz;\n"
	"layout(location = 5) flat out vec4 vTint;\n"
	"layout(location = 6) flat out vec3 vTintRange;\n"
	"void main() {\n"
	// Engine 2D space is ortho(0, W, H, 0): origin top-left, Y down. The Y is INVERTED here rather
	// than passed through, because Diligent's Vulkan backend renders with a negative-height viewport
	// so that GL- and D3D-style projections both come out upright -- which means the shader has to
	// hand it GL-style NDC (+1 at the top). Passing Vulkan-native NDC put the status bar along the
	// top of the screen, mirrored.
	"    vec2 ndc = vec2(aPos.x * uInvScreen.x * 2.0 - 1.0, 1.0 - aPos.y * uInvScreen.y * 2.0);\n"
	"    gl_Position = vec4(ndc, 0.0, 1.0);\n"
	"    vUV = aUV;\n"
	"    vColor = aColor;\n"
	"    vTexMode = int(aTexMode);\n"
	"    vFixed = aFixed;\n"
	"    vFuzz = aFuzz;\n"
	"    vTint = aTint;\n"
	"    vTintRange = aTintRange;\n"
	"}\n";

// [rc4l] The texture modes from gl_interface.h's TexMode, transcribed from getTexel() in main.fp.
//
// TM_MASK is the one that matters: coloured text is drawn with the glyph as a pure coverage MASK and
// the vertex colour supplying RGB. Multiplying by the texel instead -- which is what a naive
// `t * vColor` does -- renders every font muddy and dark, and it looks like a palette problem rather
// than a texture-mode one. The mode is captured per quad off the render state.
static const char *k2DPS =
	"#version 450\n"
	"layout(location = 0) in vec2 vUV;\n"
	"layout(location = 1) in vec4 vColor;\n"
	"layout(location = 2) flat in int vTexMode;\n"
	"layout(location = 3) flat in float vFixed;\n"
	"layout(location = 4) flat in float vFuzz;\n"
	"layout(location = 5) flat in vec4 vTint;\n"
	"layout(location = 6) flat in vec3 vTintRange;\n"
	FUA_SCREEN2D_GLSL
	FUA_FIXED_COLORMAP_GLSL
	FUA_FUZZ_GLSL
	FUA_DAMAGE_TINT_GLSL
	"layout(binding = 1) uniform sampler2D uTex;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() {\n"
	// [rc4l] A fuzzy weapon under gl_fuzztype takes GL's fuzz shader on its texel, the jagged one sampling a splat of its coordinate.
	"    int fuzz = int(vFuzz + 0.5);\n"
	"    vec4 t = texture(uTex, fuzz == 5 ? fuaFuzzSplat(vUV, uInvScreen.z) : vUV);\n"
	"    if (vTexMode == 1)      t.rgb = vec3(1.0);\n"                       // TM_MASK
	"    else if (vTexMode == 2) t.a = 1.0;\n"                               // TM_OPAQUE
	"    else if (vTexMode == 3) t = vec4(1.0-t.r, 1.0-t.b, 1.0-t.g, t.a);\n"// TM_INVERSE
	"    else if (vTexMode == 4) t = vec4(1.0, 1.0, 1.0, t.r);\n"            // TM_REDTOALPHA
	"    else if (vTexMode == 5 && (vUV.t < 0.0 || vUV.t > 1.0)) t.a = 0.0;\n"// TM_CLAMPY
	/* [rc4l] getTexel's damage tint on the weapon or the status bar face, before the fuzz shader takes the texel. */
	"    t = fuaDamageTint(t, vUV.t, vTint, vTintRange);\n"
	"    if (fuzz > 0) t = fuaFuzz(fuzz, vUV, uInvScreen.z, t);\n"
	"    outColor = t * vColor;\n"
	"    if (vFixed > 0.5 && uFixedColormap.w > 0.5) outColor.rgb = fuaFixedColormap(outColor.rgb);\n"
	"}\n";

// [rc4l] The 2D constants on both stages of a 2D pipeline.
static void BindScreen2D(Diligent::IPipelineState *pso)
{
	for (Diligent::SHADER_TYPE stage : { Diligent::SHADER_TYPE_VERTEX, Diligent::SHADER_TYPE_PIXEL })
		if (auto *v = pso->GetStaticVariableByName(stage, "Screen")) v->Set(g_cb2D);
}

static bool Ensure2DPipeline()
{
	if (g_pso2D[0]) return true;
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;

	Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
	vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua 2D VS", k2DVS);
	ps = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua 2D PS", k2DPS);
	if (!vs || !ps) return false;

	Diligent::BufferDesc cbd;
	cbd.Name = "fua 2D constants";
	cbd.Size = sizeof(float) * 12;
	cbd.Usage = Diligent::USAGE_DYNAMIC;
	cbd.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
	cbd.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
	dev->CreateBuffer(cbd, nullptr, &g_cb2D);
	if (!g_cb2D) return false;

	Diligent::LayoutElement layout[] = {
		Diligent::LayoutElement{0, 0, 2, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{2, 0, 4, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{3, 0, 1, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{4, 0, 1, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{5, 0, 1, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{6, 0, 4, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{7, 0, 3, Diligent::VT_FLOAT32, false},
	};
	static Diligent::ShaderResourceVariableDesc vars[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	Diligent::SamplerDesc samp;
	// [rc4l] CLAMP, and no mips: the engine draws 2D with CLAMP_XY_NOMIP. Wrapping would bleed the
	// opposite edge of a font glyph into its neighbour, and a mipped status bar goes soft.
	samp.MinFilter = Diligent::FILTER_TYPE_POINT;
	samp.MagFilter = Diligent::FILTER_TYPE_POINT;
	samp.MipFilter = Diligent::FILTER_TYPE_POINT;
	samp.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
	samp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
	const Diligent::ImmutableSamplerDesc samplers[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", samp },
		{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", samp },
	};

	Diligent::GraphicsPipelineStateCreateInfo pci;
	pci.PSODesc.Name = "fua 2D PSO";
	pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
	pci.GraphicsPipeline.NumRenderTargets = 1;
	pci.GraphicsPipeline.RTVFormats[0] = swap->GetDesc().ColorBufferFormat;
	pci.GraphicsPipeline.DSVFormat = swap->GetDesc().DepthBufferFormat;
	pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
	pci.GraphicsPipeline.RasterizerDesc.ScissorEnable = true;
	pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
	pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
	{
		auto &rt = pci.GraphicsPipeline.BlendDesc.RenderTargets[0];
		rt.BlendEnable = true;
		rt.SrcBlend  = Diligent::BLEND_FACTOR_SRC_ALPHA;
		rt.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
		rt.BlendOp = Diligent::BLEND_OPERATION_ADD;
		rt.SrcBlendAlpha  = Diligent::BLEND_FACTOR_ONE;
		rt.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
		rt.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
	}
	pci.GraphicsPipeline.InputLayout.LayoutElements = layout;
	pci.GraphicsPipeline.InputLayout.NumElements = sizeof(layout) / sizeof(layout[0]);
	pci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
	pci.PSODesc.ResourceLayout.Variables = vars;
	pci.PSODesc.ResourceLayout.NumVariables = 1;
	pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
	pci.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
	pci.pVS = vs;
	pci.pPS = ps;
	for (int mode = 0; mode < 3; mode++)
	{
		auto &rt = pci.GraphicsPipeline.BlendDesc.RenderTargets[0];
		rt.BlendEnable = true;
		if (mode == 2)
		{
			// GL_DST_COLOR / GL_ZERO -- the screen multiplied by the blend colour.
			rt.SrcBlend  = Diligent::BLEND_FACTOR_DEST_COLOR;
			rt.DestBlend = Diligent::BLEND_FACTOR_ZERO;
		}
		else
		{
			rt.SrcBlend  = Diligent::BLEND_FACTOR_SRC_ALPHA;
			rt.DestBlend = (mode == 1) ? Diligent::BLEND_FACTOR_ONE
			                           : Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
		}
		dev->CreateGraphicsPipelineState(pci, &g_pso2D[mode]);
		if (!g_pso2D[mode]) return false;
		BindScreen2D(g_pso2D[mode]);
	}
	{
		auto &rt = pci.GraphicsPipeline.BlendDesc.RenderTargets[0];
		rt.SrcBlend  = Diligent::BLEND_FACTOR_SRC_ALPHA;
		rt.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
		Diligent::SamplerDesc wrap = samp;
		wrap.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
		wrap.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
		const Diligent::ImmutableSamplerDesc wrapSamplers[] = {
			{ Diligent::SHADER_TYPE_PIXEL, "uTex", wrap },
			{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", wrap },
		};
		pci.PSODesc.Name = "fua 2D polygon PSO";
		pci.PSODesc.ResourceLayout.ImmutableSamplers = wrapSamplers;
		dev->CreateGraphicsPipelineState(pci, &g_pso2DWrap);
		if (!g_pso2DWrap) return false;
		BindScreen2D(g_pso2DWrap);
	}
	return true;
}

void Draw2D(Diligent::IDeviceContext *ctx)
{
	g_draws2D = g_quads2D = 0;
	if (!fua_dg_hud) return;

	int nq = 0;
	const zx::hwrender::Quad2D *quads = zx::hwrender::Quads2D(nq);
	if (quads == NULL || nq <= 0) return;
	if (!Ensure2DPipeline()) return;

	int scrW = 0, scrH = 0;
	zx::hwrender::GetScreen2D(scrW, scrH);
	if (scrW <= 0 || scrH <= 0) return;

	// [rc4l] Rebuilt once per engine frame, gated on the generation counter -- same reason as the
	// sprite stream: DrawSceneOnce runs many times between frame boundaries and Vulkan's dynamic heap
	// is per frame, not per draw.
	static TArray<Vertex2D> vb;
	static unsigned int built2D = 0;
	// [rc4l] The fixed colormap this frame's weapon was recorded under, one for the whole frame as GL's render state holds one.
	static int fixed2D = 0;
	const unsigned int gen = zx::hwrender::Generation2D();
	if (gen != built2D)
	{
		built2D = gen;
		vb.Clear();
		fixed2D = 0;
		for (int i = 0; i < nq; i++)
		{
			const zx::hwrender::Quad2D &q = quads[i];
			Vertex2D v[4];
			if (q.kind == zx::draw2d::kQuadLine)
			{
				// [rc4l] A line, widened into a quad along its own perpendicular. Doing it here rather
				// than with a line topology keeps every 2D record in one buffer and one draw order,
				// which is what a painter's-algorithm layer needs.
				const float dx = q.lx2 - q.x, dy = q.ly2 - q.y;
				const float len = sqrtf(dx * dx + dy * dy);
				const float nx = (len > 0.0001f) ? (-dy / len) * 0.5f : 0.5f;
				const float ny = (len > 0.0001f) ? ( dx / len) * 0.5f : 0.0f;
				v[0].x = q.x   + nx; v[0].y = q.y   + ny;
				v[1].x = q.x   - nx; v[1].y = q.y   - ny;
				v[2].x = q.lx2 + nx; v[2].y = q.ly2 + ny;
				v[3].x = q.lx2 - nx; v[3].y = q.ly2 - ny;
				for (int k = 0; k < 4; k++) { v[k].u = 0.f; v[k].v = 0.f; }
			}
			else if (q.kind == zx::draw2d::kQuadTri)
			{
				// [rc4l] One triangle of a polygon's fan; the record's second triangle folds onto its third corner and draws nothing.
				v[0].x = q.x;   v[0].y = q.y;   v[0].u = q.u1; v[0].v = q.v1;
				v[1].x = q.lx2; v[1].y = q.ly2; v[1].u = q.u2; v[1].v = q.v2;
				v[2].x = q.x3;  v[2].y = q.y3;  v[2].u = q.u3; v[2].v = q.v3;
				v[3] = v[2];
			}
			else
			{
			v[0].x = q.x;       v[0].y = q.y;       v[0].u = q.u1; v[0].v = q.v1;
			v[1].x = q.x;       v[1].y = q.y + q.h; v[1].u = q.u1; v[1].v = q.v2;
			v[2].x = q.x + q.w; v[2].y = q.y;       v[2].u = q.u2; v[2].v = q.v1;
			v[3].x = q.x + q.w; v[3].y = q.y + q.h; v[3].u = q.u2; v[3].v = q.v2;
			}
			if (q.fixedColormap != 0) fixed2D = q.fixedColormap;
			for (int k = 0; k < 4; k++)
			{
				v[k].r = q.r; v[k].g = q.g; v[k].b = q.b; v[k].a = q.a; v[k].texMode = (float)q.texMode; v[k].fixedCm = q.fixedColormap != 0 ? 1.f : 0.f; v[k].fuzz = (float)q.fuzz;
				v[k].tint[0] = q.tint.r; v[k].tint[1] = q.tint.g; v[k].tint[2] = q.tint.b; v[k].tint[3] = q.tint.strength;
				v[k].tintRange[0] = q.tint.top; v[k].tintRange[1] = q.tint.bottom; v[k].tintRange[2] = q.tint.glow ? 1.f : 0.f;
			}
			vb.Push(v[0]); vb.Push(v[1]); vb.Push(v[2]);
			vb.Push(v[2]); vb.Push(v[1]); vb.Push(v[3]);
		}
	if (vb.Size() == 0) return;

		if (!UploadGrowingVB(ctx, g_vb2D, g_vb2DCapacity, &vb[0], vb.Size(), sizeof(Vertex2D), 1024, "fua 2D VB")) return;
	}
	if (vb.Size() == 0 || !g_vb2D) return;

	{
		Diligent::MapHelper<float> cb(ctx, g_cb2D, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		cb[0] = 1.0f / scrW; cb[1] = 1.0f / scrH; cb[2] = ComputeWarpTimer((unsigned int)gl_frameMS, 1.f); cb[3] = 0.f;
		ComputeFixedColormapConstants(zx::viewsetup::FixedColormapShadeFor(fixed2D), &cb[4]);
	}

	Diligent::IBuffer *vbs[] = { g_vb2D };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	// The pipeline is chosen per quad now, so this only primes it.
	int curBlend = -1;

	// The backend window is its own size, so the engine's scissor rects scale with the viewport.
	const int fw = FrameWidth(), fh = FrameHeight();
	const float sx = (float)fw / scrW, sy = (float)fh / scrH;

	const void *curMat = (const void *)(size_t)-1;
	int curTrans = -99999;
	int cl = -1, ctp = -1, cr = -1, cb2 = -1;
	for (unsigned i = 0; i < (unsigned)nq; i++)
	{
		const zx::hwrender::Quad2D &q = quads[i];
		// [rc4l] GL's own vid_fps line; this window shows the backend's (k2DBackendOnly).
		if (q.target == zx::hwrender::k2DGLOnly) continue;
		// [rc4l] A polygon's triangles take the repeating pipeline, numbered past the three blends.
		const int blendMode = q.kind == zx::draw2d::kQuadTri ? 3 : (q.blend >= 0 && q.blend < 3) ? q.blend : 0;
		Diligent::IPipelineState *pso = blendMode == 3 ? g_pso2DWrap.RawPtr() : g_pso2D[blendMode].RawPtr();
		if (blendMode != curBlend)
		{
			curBlend = blendMode;
			ctx->SetPipelineState(pso);
			// A pipeline change drops what was committed, so the next quad has to commit again even
			// if it wants the same material.
			curMat = (const void *)(size_t)-1;
		}
		// [rc4l] Only ask for the binding when the material actually changed.
		//
		// This is a lookup keyed on (material, translation) and it ran once per QUAD -- for a status
		// bar that is the same handful of textures over and over, and for a line of text it is the
		// same font page for every glyph.
		if (q.material != curMat || q.translation != curTrans)
		{
			auto *found = GetMaterialSRB(pso, q.material, q.translation);
			if (!found) continue;
			ctx->CommitShaderResources(found, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			curMat = q.material;
			curTrans = q.translation;
		}

		if (q.clipL != cl || q.clipT != ctp || q.clipR != cr || q.clipB != cb2)
		{
			cl = q.clipL; ctp = q.clipT; cr = q.clipR; cb2 = q.clipB;
			Diligent::Rect sc;
			sc.left   = (Diligent::Int32)(cl * sx);
			sc.top    = (Diligent::Int32)(ctp * sy);
			sc.right  = (Diligent::Int32)(cr * sx);
			sc.bottom = (Diligent::Int32)(cb2 * sy);
			if (sc.right <= sc.left || sc.bottom <= sc.top)
			{ sc.left = 0; sc.top = 0; sc.right = (Diligent::Int32)fw; sc.bottom = (Diligent::Int32)fh; }
			ctx->SetScissorRects(1, &sc, fw, fh);
		}

		// [rc4l] One draw per quad, and that is fine.
		//
		// Merging runs of quads in the same state went in alongside the binding cache above and was
		// measured afterwards: a frame's HUD is SIXTEEN quads, and the merge took it to thirteen
		// draws. The pass costing 1.2 ms was the binding lookup, not the submissions -- so the merge
		// came out again rather than stay as untested cleverness in a painter's-order path where
		// getting the run condition wrong puts the status bar behind the world.
		Diligent::DrawAttribs draw;
		draw.NumVertices = 6;
		draw.StartVertexLocation = (Diligent::Uint32)i * 6;
		draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
		ctx->Draw(draw);
		g_draws2D++;
	}
	g_quads2D = nq;
}

void Get2DStats(int &quads, int &draws) { quads = g_quads2D; draws = g_draws2D; }

}} // namespace zx::hwrender
