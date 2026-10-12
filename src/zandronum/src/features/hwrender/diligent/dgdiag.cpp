// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Measurements and readbacks: the scene bench and scale probe, the statistics report, replay capture and screenshots, and their console commands.

#include "features/hwrender/diligent/dgscene_internal.h"

namespace zx { namespace hwrender {

bool SceneBench(int frames, FString &report)
{
	if (!g_scene.vb || !g_pso.opaque) { report = "call fua_diligent_scene first"; return false; }
	if (frames < 1) frames = 1;

	for (int i = 0; i < 20; i++) DrawSceneOnce(true, true);   // warmup
	GetContext()->WaitForIdle();

	// Presented frames -- what the backend actually costs end to end. Pump every 60th frame so the
	// window stays responsive without the pump dominating the measurement.
	const DWORD p0 = I_MSTime();
	for (int i = 0; i < frames; i++) DrawSceneOnce(true, (i % 60) == 0);
	GetContext()->WaitForIdle();
	const DWORD p1 = I_MSTime();

	// [rc4l] Submission only, so the number is the backend's draw cost for the baked geometry.
	for (int i = 0; i < 20; i++) DrawSceneOnce(false, false);
	GetContext()->WaitForIdle();
	const DWORD s0 = I_MSTime();
	for (int i = 0; i < frames; i++) DrawSceneOnce(false, false);
	GetContext()->WaitForIdle();
	const DWORD s1 = I_MSTime();

	const double ptotal = (double)(p1 - p0);
	const double stotal = (double)(s1 - s0);

	// [rc4l] Real GPU time, from a timestamp query.
	//
	// The presented figure moved 1.68 -> 0.27 ms/frame between two runs of IDENTICAL geometry, while
	// submit-only did not budge. That swing is the compositor: whether the backend window happens to
	// be occluded decides how long Present blocks, and it has nothing to do with the renderer. A
	// number that answers "is this backend fast" cannot be a number the window manager gets a vote
	// in, so ask the GPU how long it actually spent.
	double gpuMs = -1.0;
	if (GetDevice()->GetDeviceInfo().Features.TimestampQueries)
	{
		Diligent::QueryDesc qd;
		qd.Name = "fua scene duration";
		qd.Type = Diligent::QUERY_TYPE_DURATION;
		Diligent::RefCntAutoPtr<Diligent::IQuery> q;
		GetDevice()->CreateQuery(qd, &q);
		if (q)
		{
			const int gframes = frames < 64 ? frames : 64;
			auto *ctx = GetContext();
			ctx->BeginQuery(q);
			for (int i = 0; i < gframes; i++) DrawSceneOnce(false, false);
			ctx->EndQuery(q);
			ctx->WaitForIdle();
			Diligent::QueryDataDuration d;
			if (q->GetData(&d, sizeof(d)) && d.Frequency != 0)
				gpuMs = 1000.0 * (double)d.Duration / (double)d.Frequency / gframes;
		}
	}

	FString gpu;
	if (gpuMs >= 0.0) gpu.Format(", GPU %.4f ms/frame", gpuMs);
	else gpu = ", GPU n/a (no timestamp queries)";

	report.Format("Diligent: %d frames, %d verts (%d tris), %d batches, %d textures -- "
		"submit-only %.4f ms/frame%s; presented %.4f ms/frame (%.0f fps, includes compositor wait)",
		frames, g_scene.verts, g_scene.verts / 3, (int)g_scene.batches.Size(), MaterialCount(),
		stotal / frames, gpu.GetChars(),
		ptotal / frames, 1000.0 * frames / (ptotal > 0 ? ptotal : 1));
	return true;
}

void DynStats(FString &report)
{
	int sprites = zx::levelmesh::SpritePieceCount();
	int quads2D = 0, draws2D = 0;
	Get2DStats(quads2D, draws2D);
	report.Format("2D: %d quads -> %d draws | dynamic: %d sprites -> %d draws (%d submitted), %d tris "
		"[opaque %d, translucent %d, additive %d, fuzz %d]",
		quads2D, draws2D, sprites, g_stats.dynDraws, g_stats.blendSubmits, g_stats.dynTris,
		g_stats.dynByBlend[0], g_stats.dynByBlend[1], g_stats.dynByBlend[2], g_stats.dynByBlend[3]);
	FString anim;
	anim.Format(" | %d animation frame swaps since load | light buffer %d vec4s (~%d lights)",
		g_stats.animSwaps, LightCount() * 2, LightCount());
	FString geo;
	geo.Format(" | geometry: %d scene rebuilds since load, last dirty %u..%u, patches abandoned for %d translucent moved, %d blend changed, %d material, last %s",
		g_stats.geomRebuilds, g_stats.lastDirtyLo, g_stats.lastDirtyHi, g_stats.bailBlend, g_stats.bailBlendedSlot, g_stats.bailMaterial,
		g_stats.bailLast.IsNotEmpty() ? g_stats.bailLast.GetChars() : "none");
	FString cull;
	cull.Format(" | batches: %d drawn in %d draw calls | bindless %s: %d slots, %d white",
		g_stats.batchesDrawn, g_stats.worldDraws,
		BindlessActive() ? "on" : "off", MaterialSlotCount(), g_stats.matSlotsWhite);
	cull.AppendFormat(", dyn slots: %d seen, max %d, %d refused, %d with no material, %d past the fill (%d since load)",
		g_stats.dynSlotSeen, g_stats.dynSlotMax, g_stats.dynSlotRefused, g_stats.dynSlotNoMaterial, g_stats.dynSlotUnfilled, g_stats.dynSlotUnfilledTotal);
	cull.AppendFormat(", dyn draws: %d bindless, %d per-material, pipelines [%s]",
		g_stats.dynBindless, g_stats.dynPerMaterial, g_stats.fillState);
	cull.AppendFormat(", sky surfaces: %d draws since load", SkyDraws());
	cull.AppendFormat(", runs from mirror walks: %d", g_stats.dynMirrorRuns);
	cull.AppendFormat(", dyn build %.3f ms", g_stats.dynBuildMs);
	{
		// [rc4l] How much of the world the DERIVATION built, rather than transcribed from GL. The
		// number is the point of features/surfaces: it goes up as categories move across.
		int derived = 0, fellBack = 0;
		zx::surfaces::GetDeriveStats(derived, fellBack);
		int mid = 0, special = 0, notex = 0, nospan = 0, seam = 0;
		zx::surfaces::GetDeriveFallbacks(mid, special, notex, nospan, seam);
		cull.AppendFormat(" | derived surfaces: %d built, %d from the capture"
			" (%d two-sided middles, %d special walls, %d no texture, %d no span, %d seam-split)",
			derived, fellBack, mid, special, notex, nospan, seam);
	}
	cull.AppendFormat(" | array built %d times, %d remap uploads, slots 1..7 got:", g_stats.fillCount, g_stats.remapUploads);
	for (int fi = 1; fi < 8; fi++)
		cull.AppendFormat(" %d=%s(%p)", fi, g_stats.fillNames[fi].IsNotEmpty() ? g_stats.fillNames[fi].GetChars() : "-",
			g_stats.fillViews[fi]);
	report += cull;
	FString phases;
	PhaseReport(phases);
	report += " | ";
	report += phases;
	report.AppendFormat(" | world passes last frame: %d, %.2f ms CPU:%s", g_stats.lastFramePasses, g_stats.lastFrameWorldMs,
		g_stats.lastFramePassList.GetChars());
	report.AppendFormat(" | frame phases (ms): geometry %.2f, lights %.2f, clusters %.2f, constants %.2f, animation %.2f, draw %.2f",
		g_stats.lastFramePhase[0], g_stats.lastFramePhase[1], g_stats.lastFramePhase[2], g_stats.lastFramePhase[3], g_stats.lastFramePhase[4], g_stats.lastFramePhase[5]);
	int added = 0, resized = 0, rebatched = 0;
	zx::levelmesh::MeshLayoutReasons(added, resized, rebatched);
	FString why;
	why.Format(" | layout gen %u (built %u): %d added, %d resized, %d rebatched, %d repaints applied in %d uploads",
		zx::levelmesh::MeshLayoutGeneration(), g_scene.builtLayoutGen, added, resized, rebatched,
		g_stats.geomRepaints, g_stats.repaintRuns);
	report += why;
	FString patches;
	patches.Format(" | %d geometry patches (%d verts moved), %d appends (%d new, %d resized, %d revived) (%d verts, %d%% of slack)",
		g_stats.geomPatches, g_stats.geomPatchVerts, g_stats.geomAppends, g_stats.appendNoSlot, g_stats.appendResized, g_stats.slotRevived, g_scene.appendedVerts,
		g_scene.vbCapacity ? (int)(100.0 * g_scene.appendedVerts / (g_scene.vbCapacity / 8.0)) : 0);
	report += patches;
	report += geo;
	// [rc4l] What the grid actually did this frame. "0 cells" with lights alive means the binning
	// pass bailed -- a screen bigger than the table, or the cvar off -- and the shader silently fell
	// back to testing every light, which is correct but is not the thing being measured.
	AppendLightStats(report);
	report += anim;
}

// [rc4l] Measure GPU time for the current scene at a given draw multiplier.
static double MeasureGpuMs(int frames)
{
	if (!GetDevice()->GetDeviceInfo().Features.TimestampQueries) return -1.0;
	Diligent::QueryDesc qd;
	qd.Name = "fua scale duration";
	qd.Type = Diligent::QUERY_TYPE_DURATION;
	Diligent::RefCntAutoPtr<Diligent::IQuery> q;
	GetDevice()->CreateQuery(qd, &q);
	if (!q) return -1.0;

	auto *ctx = GetContext();
	for (int i = 0; i < 5; i++) DrawSceneOnce(false, false);   // warmup at this multiplier
	ctx->WaitForIdle();

	ctx->BeginQuery(q);
	for (int i = 0; i < frames; i++) DrawSceneOnce(false, false);
	ctx->EndQuery(q);
	ctx->WaitForIdle();

	Diligent::QueryDataDuration d;
	if (!q->GetData(&d, sizeof(d)) || d.Frequency == 0) return -1.0;
	return 1000.0 * (double)d.Duration / (double)d.Frequency / frames;
}

// [rc4l] The scale probe: how much geometry can this GPU draw before it costs a frame's worth of
// time? The whole argument for deleting the BSP walk rests on the answer. If the visible set costs
// 0.013 ms and 50x the visible set still costs well under a millisecond, then culling on the CPU is
// buying nothing that drawing everything would not give for free.
//
// Reported as ms and as an implied budget in triangles, because "how many triangles fit in 1 ms" is
// the form the answer is actually needed in.
bool SceneScale(int frames, FString &report)
{
	if (!g_scene.vb || !g_pso.opaque) { report = "call fua_diligent_scene first"; return false; }
	if (frames < 1) frames = 60;

	static const int kSteps[] = { 1, 2, 5, 10, 25, 50, 100 };
	const int tris = g_scene.verts / 3;
	FString out;
	out.Format("Diligent scale probe: %d tris/copy, %d batches\n", tris, (int)g_scene.batches.Size());

	const int saved = g_frame.drawRepeat;
	double perTriMs = -1.0;
	for (unsigned s = 0; s < sizeof(kSteps) / sizeof(kSteps[0]); s++)
	{
		g_frame.drawRepeat = kSteps[s];
		const double ms = MeasureGpuMs(frames);
		if (ms < 0.0) { out += "  timestamp queries unavailable\n"; break; }
		FString line;
		line.Format("  %3dx = %8d tris, %4d draws -> GPU %7.4f ms\n",
			kSteps[s], tris * kSteps[s], (int)g_scene.batches.Size() * kSteps[s], ms);
		out += line;
		perTriMs = ms / (double)(tris * kSteps[s]);
	}
	g_frame.drawRepeat = saved;

	if (perTriMs > 0.0)
	{
		FString line;
		line.Format("  => ~%.0f tris per GPU millisecond at this resolution and shader",
			1.0 / perTriMs);
		out += line;
	}
	report = out;
	return true;
}

// [rc4l] Read the swapchain back and write a PNG from inside the engine.
//
// Screen-grabbing the backend window is not a verification: it captures whatever is in front of it,
// which on a busy desktop is another window entirely. Reading the actual backbuffer is the only way
// to prove the render is correct, and it is also how a CI check would do it.
#ifdef ZX_ENABLE_REPLAY
// [rc4l] Per-frame swapchain readback for the instant replay, double-buffered.
//
// Two staging textures, alternating: this frame is copied into one while the OTHER -- copied a frame
// ago and therefore long since complete -- is mapped and read. That is the same trick the GL path
// plays with its pair of PBOs, and for the same reason: mapping the texture you just wrote forces a
// WaitForIdle, which turns a 0.03 ms frame into a pipeline stall. SceneScreenshot can afford that
// because it happens once; thirty times a second it would be the most expensive thing in the frame.
//
// The first frame after startup reads a staging texture nothing has been copied into yet, so the
// pair is not consumed until both have been written once.
static Diligent::RefCntAutoPtr<Diligent::ITexture> g_replayStaging[2];
static int g_replayStagingW = 0, g_replayStagingH = 0;
static int g_replayIndex = 0;
static bool g_replayPrimed = false;

static void ReleaseReplayStaging()
{
	g_replayStaging[0].Release();
	g_replayStaging[1].Release();
	g_replayStagingW = g_replayStagingH = 0;
	g_replayIndex = 0;
	g_replayPrimed = false;
}

void CaptureReplayFrame(Diligent::IDeviceContext *ctx, Diligent::ISwapChain *swap)
{
	if (!zx::replay::WantsFrameVulkan()) return;

	auto *dev = GetDevice();
	if (!dev || !ctx || !swap) return;
	auto *backTex = swap->GetCurrentBackBufferRTV()->GetTexture();
	if (!backTex) return;
	const auto &bd = backTex->GetDesc();

	if (!g_replayStaging[0] || g_replayStagingW != (int)bd.Width || g_replayStagingH != (int)bd.Height)
	{
		ReleaseReplayStaging();
		Diligent::TextureDesc sd;
		sd.Name = "fua replay readback";
		sd.Type = Diligent::RESOURCE_DIM_TEX_2D;
		sd.Width = bd.Width; sd.Height = bd.Height;
		sd.Format = bd.Format;
		sd.Usage = Diligent::USAGE_STAGING;
		sd.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
		sd.BindFlags = Diligent::BIND_NONE;
		sd.MipLevels = 1;
		for (int i = 0; i < 2; i++) dev->CreateTexture(sd, nullptr, &g_replayStaging[i]);
		if (!g_replayStaging[0] || !g_replayStaging[1]) { ReleaseReplayStaging(); return; }
		g_replayStagingW = (int)bd.Width; g_replayStagingH = (int)bd.Height;
	}

	const int write = g_replayIndex;
	const int read = g_replayIndex ^ 1;

	// [rc4l] Unbind the render targets before copying FROM the back buffer.
	//
	// Copying a texture that is still bound as a render target makes Diligent unbind it for you and
	// say so, every single frame -- 1719 pairs of info messages in one short session, 94% of the
	// engine log. This is the fix the message itself asks for, and doing it here costs nothing:
	// the capture runs immediately before Present, so nothing draws afterwards.
	ctx->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);

	Diligent::CopyTextureAttribs cta;
	cta.pSrcTexture = backTex;
	cta.SrcTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	cta.pDstTexture = g_replayStaging[write];
	cta.DstTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ctx->CopyTexture(cta);

	if (g_replayPrimed)
	{
		Diligent::MappedTextureSubresource mapped;
		// DO_NOT_WAIT: if last frame's copy is somehow still in flight, skip this frame rather than
		// stall. A dropped capture frame is invisible; a stall is not.
		ctx->MapTextureSubresource(g_replayStaging[read], 0, 0, Diligent::MAP_READ,
			Diligent::MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
		if (mapped.pData != nullptr)
		{
			const int w = g_replayStagingW, h = g_replayStagingH;
			// Channel order from the FORMAT, never assumed -- a hard-coded BGRA swap once created a
			// red/blue inversion that still looked like a plausible Doom scene, just cooler.
			const bool bgra = (bd.Format == Diligent::TEX_FORMAT_BGRA8_UNORM ||
			                   bd.Format == Diligent::TEX_FORMAT_BGRA8_UNORM_SRGB);
			const int ri = bgra ? 2 : 0, bi = bgra ? 0 : 2;

			static TArray<unsigned char> rgb;
			rgb.Resize((unsigned)(w * h * 3));
			for (int y = 0; y < h; y++)
			{
				const unsigned char *srcRow = (const unsigned char *)mapped.pData + (size_t)y * mapped.Stride;
				unsigned char *dstRow = &rgb[0] + (size_t)y * w * 3;
				for (int x = 0; x < w; x++)
				{
					dstRow[x*3+0] = srcRow[x*4+ri];
					dstRow[x*3+1] = srcRow[x*4+1];
					dstRow[x*3+2] = srcRow[x*4+bi];
				}
			}
			ctx->UnmapTextureSubresource(g_replayStaging[read], 0, 0);
			// Already top-down, so a positive pitch.
			zx::replay::SubmitFrameVulkan(&rgb[0], w, h, w * 3);
		}
	}

	g_replayIndex = read;
	g_replayPrimed = true;
}
#endif // ZX_ENABLE_REPLAY

bool SceneScreenshot(const char *path, FString &report)
{
	if (!g_scene.vb || !g_pso.opaque) { report = "call fua_diligent_scene first"; return false; }

	auto *ctx = GetContext();
	auto *swap = GetSwapChain();
	auto *dev = GetDevice();

	// [rc4l] Always from the CURRENT camera. The MVP used to be whatever `fua_diligent_scene` had
	// snapshotted, so a screenshot taken after the player turned showed the room they used to be
	// looking at -- and every GL/Vulkan comparison made that way was of two different viewpoints.
	// That cost a long detour: MAP01 looked like it was missing half its geometry when it was simply
	// facing the other way.
	BuildMVP(g_mvp);

	// [rc4l] Draw WITHOUT presenting. Present flips to the next image in the swapchain, so reading
	// GetCurrentBackBufferRTV afterwards returns a buffer whose contents are undefined -- which read
	// back as a perfectly convincing all-black PNG the first time.
	DrawSceneOnce(false);
	ctx->WaitForIdle();

	auto *backTex = swap->GetCurrentBackBufferRTV()->GetTexture();
	const auto &bd = backTex->GetDesc();

	// Same as the replay path: unbind before copying from the back buffer, or Diligent does it for
	// us and logs about it.
	ctx->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);

	Diligent::TextureDesc sd;
	sd.Name = "fua readback";
	sd.Type = Diligent::RESOURCE_DIM_TEX_2D;
	sd.Width = bd.Width; sd.Height = bd.Height;
	sd.Format = bd.Format;
	sd.Usage = Diligent::USAGE_STAGING;
	sd.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
	sd.BindFlags = Diligent::BIND_NONE;
	sd.MipLevels = 1;
	Diligent::RefCntAutoPtr<Diligent::ITexture> staging;
	dev->CreateTexture(sd, nullptr, &staging);
	if (!staging) { report = "staging texture creation failed"; return false; }

	Diligent::CopyTextureAttribs cta;
	cta.pSrcTexture = backTex;
	cta.SrcTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	cta.pDstTexture = staging;
	cta.DstTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ctx->CopyTexture(cta);
	ctx->WaitForIdle();

	Diligent::MappedTextureSubresource mapped;
	ctx->MapTextureSubresource(staging, 0, 0, Diligent::MAP_READ, Diligent::MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
	if (mapped.pData == nullptr) { report = "readback map failed"; return false; }

	const int w = (int)bd.Width, h = (int)bd.Height;

	// [rc4l] Channel order comes from the FORMAT, not from an assumption.
	//
	// This used to hard-code a BGRA->RGB swap. When the swapchain came back RGBA that swap *created*
	// a red/blue inversion in every screenshot, and since Doom textures are muddy browns and greys
	// the result still looked like a plausible Doom scene -- just "cooler". It cost a long detour
	// hunting a lighting bug that did not exist.
	const bool bgra = (bd.Format == Diligent::TEX_FORMAT_BGRA8_UNORM ||
	                   bd.Format == Diligent::TEX_FORMAT_BGRA8_UNORM_SRGB);
	const int ri = bgra ? 2 : 0, bi = bgra ? 0 : 2;

	BYTE *rgb = new BYTE[w * h * 3];
	for (int y = 0; y < h; y++)
	{
		const BYTE *srcRow = (const BYTE *)mapped.pData + (size_t)y * mapped.Stride;
		BYTE *dstRow = rgb + (size_t)y * w * 3;
		for (int x = 0; x < w; x++)
		{
			dstRow[x*3+0] = srcRow[x*4+ri];
			dstRow[x*3+1] = srcRow[x*4+1];
			dstRow[x*3+2] = srcRow[x*4+bi];
		}
	}
	ctx->UnmapTextureSubresource(staging, 0, 0);

	FILE *f = fopen(path, "wb");
	bool ok = false;
	if (f)
	{
		ok = M_CreatePNG(f, rgb, NULL, SS_RGB, w, h, w * 3) && M_FinishPNG(f);
		fclose(f);
	}
	delete[] rgb;

	if (!ok) { report.Format("failed to write %s", path); return false; }
	// [rc4l] The camera is reported because a GL/Vulkan screenshot pair is only meaningful if both
	// used the same one, and "are these two images of the same viewpoint" turned out to be a
	// question worth answering with numbers rather than by eye -- twice.
	report.Format("wrote %s (%dx%d, swapchain format %d, %s) cam=(%.1f, %.1f, %.1f) yaw=%.1f",
		path, w, h, (int)bd.Format, bgra ? "BGRA" : "RGBA",
		FIXED2FLOAT(viewx), FIXED2FLOAT(viewy), FIXED2FLOAT(viewz),
		(float)(viewangle >> ANGLETOFINESHIFT) * 360.0f / 8192.0f);
	return true;
}

}} // namespace zx::hwrender

// [rc4l] Re-scan the level for mirror lines, for when one has just been made.
//
// CollectMirrors runs when a level is armed and never again, which is correct for a map -- a
// linedef special does not change by itself. It does change when fua_make_mirror changes it, and
// without this the backend keeps drawing that wall as a solid one while GL is already reflecting
// in it. The traced reflection had no other way to be reached, which is most of why it went
// untested for so long.
namespace zx { namespace hwrender { void StackInfo(FString &out); }}
CCMD( fua_dg_stackinfo )
{
	FString report;
	zx::hwrender::StackInfo( report );
	Printf( "%s\n", report.GetChars( ) );
}
CCMD( fua_dg_mirrors )
{
	Printf( "fua_dg_mirrors: %d mirror surface(s)\n", zx::hwrender::RecollectMirrors( ) );
}
CCMD( fua_diligent_scene )
{
	FString report;
	const bool ok = zx::hwrender::SceneUpload( report );
	Printf( "%s%s\n", ok ? "" : "FAILED: ", report.GetChars( ) );
}

CCMD( fua_diligent_shot )
{
	const char *path = ( argv.argc( ) > 1 ) ? argv[1] : "fua_vk.png";
	FString report;
	const bool ok = zx::hwrender::SceneScreenshot( path, report );
	Printf( "%s%s\n", ok ? "" : "FAILED: ", report.GetChars( ) );
}

CCMD( fua_diligent_bench )
{
	const int n = ( argv.argc( ) > 1 ) ? atoi( argv[1] ) : 300;
	FString report;
	const bool ok = zx::hwrender::SceneBench( n, report );
	Printf( "%s%s\n", ok ? "" : "FAILED: ", report.GetChars( ) );
}

CCMD( fua_dg_dynstats )
{
	FString report;
	zx::hwrender::DynStats( report );
	Printf( "%s\n", report.GetChars( ) );
}

// [rc4l] What a static shader array would actually cost, before adding one.
//
// A bindless material array is declared STATIC, and Diligent copies a pipeline's static resources
// into EVERY shader resource binding made from it. This backend makes one SRB per (pipeline,
// material, translation) -- see g_matSRBs -- so an N-slot array does not cost N descriptors, it costs
// N times the number of SRBs. That is the whole of the unexplained 64-works/128-dies ceiling the
// first bindless attempt hit: not a pool size, not a device limit, but bindless and per-material
// SRBs being mutually exclusive by construction.
//
// This prints the multiplier for the map that is loaded, so the next attempt can see the number
// before it spends an afternoon on the symptom.
// [rc4l] What each batch thinks its material is, against what the slot table says.
//
// A surface drawn with another surface's texture has exactly two causes and they need opposite
// fixes: the vertex is carrying the wrong slot number, or the slot is carrying the wrong texture.
// This prints both sides for the first batches so the two can be told apart instead of argued about.
// [rc4l] Do to the texture cache what a level change does, on demand.
//
// The cache is keyed on raw FMaterial*, and a new level puts its materials at the addresses the old
// level's just vacated -- so a cache that outlives the change serves the previous map's picture for
// a pointer that merely happens to match. That is the standing suspect for bindless coming up wrong
// after a map change and right on a fresh load, and this is how to ask it directly: if flushing here
// repairs the frame, the fault is WHEN the cache was filled, not what the array or the vertices say.
CCMD( fua_dg_flushtextures )
{
	zx::hwrender::FlushTextureCache( );
	Printf( "texture cache and every binding dropped; they rebuild next frame\n" );
}

CCMD( fua_dg_bindless_rebuild )
{
	zx::hwrender::DirtyMaterialSlots( );
	Printf( "material array will be rebuilt next frame\n" );
}

CCMD( fua_dg_slots )
{
	const int n = ( argv.argc( ) > 1 ) ? atoi( argv[1] ) : 12;
	FString report;
	zx::hwrender::SlotReport( n, report );
	Printf( "%s" "\n", report.GetChars( ) );
}

CCMD( fua_dg_srbcost )
{
	const int slots = ( argv.argc( ) > 1 ) ? atoi( argv[1] ) : 512;
	int cached = 0, batch = 0;
	zx::hwrender::MaterialSRBCounts( cached, batch );
	const int srbs = cached + batch;
	Printf( "material SRBs live: %d cached + %d batch = %d" "\n",
		cached, batch, srbs );
	Printf( "a %d-slot STATIC sampler array would add %lld combined-image-sampler descriptors" "\n",
		slots, (long long)slots * (long long)srbs );
	Printf( "one SRB per pipeline instead -- which is what bindless is FOR -- would add %d" "\n",
		slots * 13 );
}

CCMD( fua_diligent_scale )
{
	const int n = ( argv.argc( ) > 1 ) ? atoi( argv[1] ) : 60;
	FString report;
	const bool ok = zx::hwrender::SceneScale( n, report );
	Printf( "%s%s\n", ok ? "" : "FAILED: ", report.GetChars( ) );
}
