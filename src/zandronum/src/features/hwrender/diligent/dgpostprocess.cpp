// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] features/postprocess on the backend: each GLDEFS post-process shader's program, made at launch on the device's pool, run over the 3D before and after the weapon and over the whole screen, between two targets made once per size.

#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/hwrender/diligent/dggputimer.h"
#include "features/postprocess/postprocess.h"
#include "features/materials/materials.h"
#include "features/gamecopy/level.h"
#include "features/rendersettings/rendersettings.h"   // [Doruk-Aksoy] gl_frameMS
#include "features/telemetry/telemetry.h"

#include "MapHelper.hpp"
#include "c_cvars.h"
#include "doomdef.h"
#include "r_utility.h"   // [Doruk-Aksoy] r_ActorFrac
#include "w_wad.h"

#include <cstring>
#include <vector>

EXTERN_CVAR(Bool, fua_dg_async_pipelines)

namespace zx { namespace hwrender {

namespace {

namespace pp = zx::postprocess;

struct Program
{
	Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
	Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
	// [Doruk-Aksoy] Whether the compiled shader kept any resource at all, since one reading nothing is drawn without a binding.
	bool bound = true;
	Diligent::RefCntAutoPtr<Diligent::IBuffer> autoCB, userCB;
	// [Doruk-Aksoy] A binding per picture the program reads (the source, A, B), each made once for the textures it reads and never pointed elsewhere.
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb[3];
	Diligent::ITexture *srbInput[3] = { nullptr, nullptr, nullptr };
	Diligent::ITexture *srbLast[3] = { nullptr, nullptr, nullptr };
	std::vector<Diligent::RefCntAutoPtr<Diligent::ITexture>> extraTextures;
	std::vector<unsigned char> block;
	bool lastInput = false, ready = false, broken = false;
};

std::vector<Program> g_programs;
unsigned g_madeFor = ~0u;
// [Doruk-Aksoy] The two pictures passes alternate between, at the frame's size, and the previous frame's for programs that read it.
Diligent::RefCntAutoPtr<Diligent::ITexture> g_ping[2], g_last;
float g_lastRegion[4] = { 1.f, 1.f, 1.f, 1.f };
bool g_lastValid = false;
unsigned g_previousFrameMs = 0;
// [Doruk-Aksoy] This frame's programs for each stage, decided before the main view.
std::vector<int> g_chain[pp::kStageCount];
bool g_stereo = false;

void Note(const pp::ShaderDecl &d, const char *why)
{
	Printf("%s", pp::ComputePostShaderSkipNote("vulkan", d.name.empty() ? d.lump : d.name, why).c_str());
}

bool MakeConstants(const char *name, unsigned size, Diligent::RefCntAutoPtr<Diligent::IBuffer> &out)
{
	Diligent::BufferDesc bd;
	bd.Name = name;
	bd.Size = size;
	bd.Usage = Diligent::USAGE_DYNAMIC;
	bd.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
	bd.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
	CreateDeviceBuffer(bd, nullptr, &out);
	return out.RawPtr() != nullptr;
}

// [Doruk-Aksoy] A Texture line's picture, from the texture GLDEFS names as UZDoom finds it, uploaded once; null when there is none.
Diligent::RefCntAutoPtr<Diligent::ITexture> MakeTexture(const std::string &file)
{
	Diligent::RefCntAutoPtr<Diligent::ITexture> tex;
	const zx::materials::MaterialRef m = zx::materials::MaterialForName(file.c_str());
	int w = 0, h = 0;
	unsigned char *pixels = m != NULL ? zx::materials::MaterialPixels(m, 0, w, h) : NULL;
	if (pixels == NULL) return tex;
	FString name;
	name.Format("fua post texture %s", file.c_str());
	Diligent::TextureDesc td;
	td.Name = name.GetChars();
	td.Type = Diligent::RESOURCE_DIM_TEX_2D;
	td.Width = (Diligent::Uint32)w;
	td.Height = (Diligent::Uint32)h;
	td.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
	td.MipLevels = 1;
	td.BindFlags = Diligent::BIND_SHADER_RESOURCE;
	td.Usage = Diligent::USAGE_IMMUTABLE;
	Diligent::TextureSubResData sub(pixels, (Diligent::Uint64)w * 4);
	Diligent::TextureData data(&sub, 1);
	CreateDeviceTexture(td, &data, &tex);
	delete[] pixels;
	return tex;
}

// [Doruk-Aksoy] Program i's shaders started on the device's pool, its constants and Texture lines made, or left out with GL's note.
void MakeProgram(int i, Program &p)
{
	const pp::ShaderDecl &d = pp::PostShader(i);
	// [Doruk-Aksoy] A wad's lumps have only their short names, so those are tried too.
	const int lump = Wads.CheckNumForFullName(d.lump.c_str(), true);
	if (lump < 0) { p.broken = true; Note(d, "its shader lump is not loaded"); return; }
	FMemLump data = Wads.ReadLump(lump);
	const std::string adapted = pp::ComputeAdaptPostProcessShader(data.GetString().GetChars());
	pp::ProgramDesc desc;
	desc.uniforms = d.uniforms;
	desc.textureNames = d.textureNames;
	desc.lastInput = p.lastInput = pp::ComputeReadsLastInput(adapted);
	const std::string text = pp::ComputePostProcessProlog(desc) + adapted;
	FString name;
	name.Format("fua post %s", d.name.empty() ? d.lump.c_str() : d.name.c_str());
	p.vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua post VS", PictureVS());
	p.ps = CompileShaderAsync(Diligent::SHADER_TYPE_PIXEL, name.GetChars(), text.c_str());
	if (!p.vs || !p.ps || !MakeConstants("fua post automatic constants", sizeof(float) * pp::kAutoBlockFloats, p.autoCB)) { p.broken = true; Note(d, ""); return; }
	if (!d.uniforms.empty() && !MakeConstants("fua post uniforms", d.blockSize, p.userCB)) { p.broken = true; Note(d, ""); return; }
	p.block.assign(d.blockSize, 0);
	for (size_t t = 0; t < d.textureFiles.size(); t++)
	{
		p.extraTextures.push_back(MakeTexture(d.textureFiles[t]));
		if (!p.extraTextures.back()) Printf("vulkan: post-process shader %s has no texture '%s'; it reads white\n", d.name.c_str(), d.textureFiles[t].c_str());
	}
}

// [Doruk-Aksoy] Program i's pipeline started on the device's pool once its shader compiled, laid out with only the resources the shader kept: a layout naming one the SPIR-V optimizer dropped hung Diligent making a binding.
void MakePipeline(int i, Program &p)
{
	const pp::ShaderDecl &d = pp::PostShader(i);
	if (p.broken) return;
	if (p.ps->GetStatus(true) != Diligent::SHADER_STATUS_READY) { p.broken = true; Note(d, ""); return; }
	std::vector<std::string> present;
	for (Diligent::Uint32 r = 0; r < p.ps->GetResourceCount(); r++)
	{
		Diligent::ShaderResourceDesc rd;
		p.ps->GetResourceDesc(r, rd);
		present.push_back(rd.Name);
	}
	p.bound = !present.empty();
	// [Doruk-Aksoy] The pictures clamp, as UZDoom's input does; its extra textures repeat.
	Diligent::SamplerDesc clamp, wrap;
	clamp.MinFilter = clamp.MagFilter = clamp.MipFilter = Diligent::FILTER_TYPE_LINEAR;
	clamp.AddressU = clamp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
	wrap.MinFilter = wrap.MagFilter = wrap.MipFilter = Diligent::FILTER_TYPE_LINEAR;
	wrap.AddressU = wrap.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
	const std::vector<pp::LayoutTexture> layout = pp::ComputeLayoutTextures(p.lastInput, d.textureNames, present);
	std::vector<Diligent::ImmutableSamplerDesc> samplers;
	std::vector<Diligent::ShaderResourceVariableDesc> vars;
	for (const pp::LayoutTexture &t : layout)
	{
		samplers.push_back(Diligent::ImmutableSamplerDesc(Diligent::SHADER_TYPE_PIXEL, t.name.c_str(), t.clamp ? clamp : wrap));
		vars.push_back(Diligent::ShaderResourceVariableDesc(Diligent::SHADER_TYPE_PIXEL, t.name.c_str(), Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE));
	}
	FString psoName;
	psoName.Format("fua post %s PSO", d.name.empty() ? d.lump.c_str() : d.name.c_str());
	Diligent::GraphicsPipelineStateCreateInfo pci;
	pci.PSODesc.Name = psoName.GetChars();
	pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
	pci.GraphicsPipeline.NumRenderTargets = 1;
	pci.GraphicsPipeline.RTVFormats[0] = GetSwapChain()->GetDesc().ColorBufferFormat;
	pci.GraphicsPipeline.DSVFormat = Diligent::TEX_FORMAT_UNKNOWN;
	pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
	pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
	pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
	pci.PSODesc.ResourceLayout.Variables = vars.data();
	pci.PSODesc.ResourceLayout.NumVariables = (Diligent::Uint32)vars.size();
	pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers.data();
	pci.PSODesc.ResourceLayout.NumImmutableSamplers = (Diligent::Uint32)samplers.size();
	pci.pVS = p.vs;
	pci.pPS = p.ps;
	if (fua_dg_async_pipelines) pci.Flags |= Diligent::PSO_CREATE_FLAG_ASYNCHRONOUS;
	CreatePipeline(pci, &p.pso);
	if (!p.pso) { p.broken = true; Note(d, ""); }
}

// [Doruk-Aksoy] Whether a program's pipeline is ready, its constants bound the first time it is.
bool Ready(int i)
{
	Program &p = g_programs[i];
	if (p.ready || p.broken) return p.ready;
	const Diligent::PIPELINE_STATE_STATUS s = p.pso->GetStatus(false);
	if (s == Diligent::PIPELINE_STATE_STATUS_COMPILING) return false;
	if (s != Diligent::PIPELINE_STATE_STATUS_READY) { p.broken = true; Note(pp::PostShader(i), ""); return false; }
	if (auto *v = p.pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, pp::kAutoBlockName)) v->Set(p.autoCB);
	if (p.userCB)
		if (auto *v = p.pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, pp::kUniformBlockName)) v->Set(p.userCB);
	p.ready = true;
	return true;
}

// [Doruk-Aksoy] The programs for GLDEFS as it was last read, every one started at once so the pool has them together.
void EnsurePrograms()
{
	if (g_madeFor == pp::PostShadersGeneration() || GetDevice() == NULL || GetSwapChain() == NULL) return;
	ReleasePostProcess();
	g_madeFor = pp::PostShadersGeneration();
	g_programs.resize((size_t)pp::PostShaderCount());
	for (int i = 0; i < pp::PostShaderCount(); i++) MakeProgram(i, g_programs[i]);
	// [Doruk-Aksoy] Each waited for in turn once all are compiling, as PrecompileUserShaders does, since its layout needs the compiled shader.
	for (int i = 0; i < pp::PostShaderCount(); i++) MakePipeline(i, g_programs[i]);
}

bool SameSize(const Diligent::RefCntAutoPtr<Diligent::ITexture> &t, int w, int h)
{
	return t && (int)t->GetDesc().Width == w && (int)t->GetDesc().Height == h;
}

// [Doruk-Aksoy] The ping-pong pictures at the frame's size, and the previous frame's when a program reads it, made again only when the frame's size changes.
bool EnsurePictures(bool last)
{
	const int w = FrameWidth(), h = FrameHeight();
	for (int k = 0; k < 2; k++)
		if (!SameSize(g_ping[k], w, h) && !CreateColorTarget(w, h, k == 0 ? "fua post A" : "fua post B", g_ping[k])) return false;
	if (last && !SameSize(g_last, w, h))
	{
		g_lastValid = false;
		if (!CreateColorTarget(w, h, "fua post last", g_last)) return false;
	}
	return true;
}

Diligent::ITexture *PictureOf(int picture, Diligent::ITexture *source)
{
	if (picture == pp::kPictureA) return g_ping[0];
	if (picture == pp::kPictureB) return g_ping[1];
	return source;
}

// [Doruk-Aksoy] Program i's binding reading `input` in `slot`, made again only when that slot's pictures were made again.
Diligent::IShaderResourceBinding *BindingFor(int i, int slot, Diligent::ITexture *input)
{
	Program &p = g_programs[i];
	Diligent::ITexture *last = p.lastInput && g_last ? g_last.RawPtr() : input;
	if (p.srb[slot] && p.srbInput[slot] == input && p.srbLast[slot] == last) return p.srb[slot];
	p.srb[slot].Release();
	p.pso->CreateShaderResourceBinding(&p.srb[slot], true);
	if (!p.srb[slot]) return nullptr;
	if (auto *v = p.srb[slot]->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, pp::kInputName))
		v->Set(input->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
	if (p.lastInput)
		if (auto *v = p.srb[slot]->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, pp::kLastInputName))
			v->Set(last->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
	const std::vector<std::string> &names = pp::PostShader(i).textureNames;
	for (size_t t = 0; t < names.size(); t++)
		if (auto *v = p.srb[slot]->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, pp::ComputeTextureVariable(names[t]).c_str()))
			v->Set(p.extraTextures[t] ? p.extraTextures[t]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE) : GetMaterialSRV(NULL, 0));
	p.srbInput[slot] = input;
	p.srbLast[slot] = last;
	return p.srb[slot];
}

// [Doruk-Aksoy] The top left w x h of `from` copied into `to`, with nothing bound to draw into while it is.
void CopyPicture(Diligent::IDeviceContext *ctx, Diligent::ITexture *from, Diligent::ITexture *to, int w, int h)
{
	ctx->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
	Diligent::Box box(0, (Diligent::Uint32)w, 0, (Diligent::Uint32)h);
	Diligent::CopyTextureAttribs c(from, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, to, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	c.pSrcBox = &box;
	ctx->CopyTexture(c);
}

// [Doruk-Aksoy] Program i reading picture `slot` (`input`, its share `region`), drawn into the top left outW x outH of `target`.
void RunPass(Diligent::IDeviceContext *ctx, int i, int slot, Diligent::ITexture *input, const float region[4], Diligent::ITextureView *target, int outW, int outH)
{
	Program &p = g_programs[i];
	if (p.lastInput && !g_lastValid && g_last)
	{
		// [Doruk-Aksoy] With no previous frame kept, it reads this one, as a shader just turned on has none yet.
		CopyPicture(ctx, input, g_last, (int)region[2], (int)region[3]);
		memcpy(g_lastRegion, region, sizeof(g_lastRegion));
		g_lastValid = true;
	}
	Diligent::IShaderResourceBinding *srb = p.bound ? BindingFor(i, slot, input) : nullptr;
	if (p.bound && srb == nullptr) return;
	const pp::EffectiveEnabled on = pp::PostShaderEnabledNow(i);
	const pp::Times times = pp::ComputePostProcessTimes((unsigned)gl_frameMS, g_previousFrameMs, zx::gamecopy::Level().maptime, FIXED2DBL(r_ActorFrac), on.sinceTic, TICRATE);
	{
		float a[pp::kAutoBlockFloats];
		pp::ComputeAutoBlock(times, region, g_lastRegion, outW, outH, a);
		Diligent::MapHelper<float> cb(ctx, p.autoCB, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		memcpy((float *)cb, a, sizeof(a));
	}
	if (p.userCB)
	{
		pp::PostShaderBlock(i, p.block.data());
		Diligent::MapHelper<Diligent::Uint8> cb(ctx, p.userCB, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		memcpy((Diligent::Uint8 *)cb, p.block.data(), p.block.size());
	}
	ctx->SetRenderTargets(1, &target, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	const Diligent::TextureDesc &td = target->GetTexture()->GetDesc();
	Diligent::Viewport vp;
	vp.Width = (float)outW;
	vp.Height = (float)outH;
	ctx->SetViewports(1, &vp, td.Width, td.Height);
	ctx->SetPipelineState(p.pso);
	if (srb != nullptr) ctx->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	Diligent::DrawAttribs draw;
	draw.NumVertices = 3;
	ctx->Draw(draw);
	ZX_COUNT("gpu/post/passes", "passes", 1);
}

// [Doruk-Aksoy] Runs `chain` from picture `first` (`source`, its share `region`), the last pass into the frame at its full size.
void RunChain(Diligent::IDeviceContext *ctx, const std::vector<int> &chain, int first, Diligent::ITexture *source, const float region[4], int w, int h,
	int weaponAt, int viewQuads, int &first2D)
{
	const std::vector<pp::Pass> passes = pp::ComputePingPong((int)chain.size(), first, true);
	for (size_t k = 0; k < passes.size(); k++)
	{
		Diligent::ITexture *input = PictureOf(passes[k].source, source);
		// [Doruk-Aksoy] The weapon goes under the scene shaders, drawn into the picture they read at its share of the frame.
		if ((int)k == weaponAt && viewQuads > 0)
		{
			Draw2D(ctx, 0, viewQuads, input->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET), w, h);
			first2D = viewQuads;
		}
		const bool last = passes[k].target == pp::kPictureFinal;
		Diligent::ITextureView *target = last ? FrameRTV() : PictureOf(passes[k].target, source)->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
		RunPass(ctx, chain[k], passes[k].source, input, region, target, last ? FrameWidth() : w, last ? FrameHeight() : h);
	}
}

bool AnyReadsLast(const std::vector<int> &chain)
{
	for (const int i : chain)
		if (g_programs[i].lastInput) return true;
	return false;
}

} // namespace

void PrecompilePostShaders()
{
	ZX_ZONE("load/post_shaders");   // [Doruk-Aksoy] every GLDEFS post-process shader's program started at launch
	EnsurePrograms();
}

void ReleasePostProcess()
{
	g_programs.clear();
	g_madeFor = ~0u;
	g_ping[0].Release();
	g_ping[1].Release();
	g_last.Release();
	g_lastValid = false;
	for (std::vector<int> &c : g_chain) c.clear();
}

bool BeginPostProcessFrame(bool stereo)
{
	for (std::vector<int> &c : g_chain) c.clear();
	g_stereo = stereo;
	if (!pp::PostProcessAllowed() || pp::PostShaderCount() == 0 || !FrameOffscreen()) return false;
	EnsurePrograms();
	// [Doruk-Aksoy] Every picture a declared shader could need is made with the frame's size from the first frame on, so turning one on in play makes nothing.
	bool reads = false;
	for (const Program &p : g_programs) reads = reads || p.lastInput;
	if (!EnsurePictures(reads) || !EnsureSceneTarget()) return false;
	std::vector<int> stages;
	std::vector<bool> on, ready;
	for (int i = 0; i < (int)g_programs.size(); i++)
	{
		stages.push_back(pp::PostShader(i).stage);
		ready.push_back(Ready(i));
		on.push_back(pp::PostShaderEnabledNow(i).enabled);
	}
	for (int s = 0; s < pp::kStageCount; s++) g_chain[s] = pp::ComputePostProcessChain(stages, on, ready, s);
	return !stereo && (!g_chain[pp::kStageBeforeBloom].empty() || !g_chain[pp::kStageScene].empty());
}

bool RunScenePostProcess(Diligent::IDeviceContext *ctx, int viewQuads, bool present, int &first2D)
{
	first2D = 0;
	const std::vector<int> &before = g_chain[pp::kStageBeforeBloom], &scene = g_chain[pp::kStageScene];
	if (before.empty() && scene.empty()) return false;
	ZX_ZONE("draw/postprocess_scene");   // [Doruk-Aksoy] the beforebloom and scene stages
	BeginGpuPass(ctx, kGpuPostScene);
	std::vector<int> chain(before);
	chain.insert(chain.end(), scene.begin(), scene.end());
	const int weaponAt = scene.empty() ? -1 : (int)before.size();
	float region[4];
	if (g_stereo)
	{
		// [Doruk-Aksoy] The eyes are already blended into the frame, so the stages read a copy of it.
		CopyPicture(ctx, FrameTexture(), g_ping[0], FrameWidth(), FrameHeight());
		pp::ComputePostRegion(FrameWidth(), FrameHeight(), FrameWidth(), FrameHeight(), region);
		RunChain(ctx, chain, pp::kPictureA, nullptr, region, FrameWidth(), FrameHeight(), weaponAt, viewQuads, first2D);
	}
	else
	{
		pp::ComputePostRegion(SceneWidth(), SceneHeight(), FrameWidth(), FrameHeight(), region);
		RunChain(ctx, chain, pp::kPictureSource, SceneTexture(), region, SceneWidth(), SceneHeight(), weaponAt, viewQuads, first2D);
	}
	// [Doruk-Aksoy] The scene as it ends up is the next frame's LastInputTexture, kept only on a presented frame and only while a program reads it.
	if (present)
	{
		g_lastValid = false;
		if (g_last && AnyReadsLast(chain))
		{
			CopyPicture(ctx, FrameTexture(), g_last, FrameWidth(), FrameHeight());
			pp::ComputePostRegion(FrameWidth(), FrameHeight(), FrameWidth(), FrameHeight(), g_lastRegion);
			g_lastValid = true;
		}
	}
	EndGpuPass(ctx, kGpuPostScene);
	return true;
}

void RunScreenPostProcess(Diligent::IDeviceContext *ctx)
{
	const std::vector<int> &chain = g_chain[pp::kStageScreen];
	if (chain.empty() || !g_ping[0]) return;
	ZX_ZONE("draw/postprocess_screen");   // [Doruk-Aksoy] the screen stage, over the 2D too
	BeginGpuPass(ctx, kGpuPostScreen);
	CopyPicture(ctx, FrameTexture(), g_ping[0], FrameWidth(), FrameHeight());
	float region[4];
	pp::ComputePostRegion(FrameWidth(), FrameHeight(), FrameWidth(), FrameHeight(), region);
	int unused = 0;
	RunChain(ctx, chain, pp::kPictureA, nullptr, region, FrameWidth(), FrameHeight(), -1, 0, unused);
	EndGpuPass(ctx, kGpuPostScreen);
}

void PostProcessPresented() { g_previousFrameMs = (unsigned)gl_frameMS; }

}} // namespace zx::hwrender
