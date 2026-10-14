// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The frame driver: arming a level for the backend, the per-frame entry point, and the present-to-present clock fua_frametimes reads.

#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/hwrender/backendonly.h"
#include "features/hwrender/computation/levelsetup_compute.h"
#include "features/mcp-bridge/computation/mcprpc_compute.h"   // [rc4l] SummarizeFrameTimes

EXTERN_CVAR(Bool, fua_vis_check)
EXTERN_CVAR(Bool, fua_sprite_check)
EXTERN_CVAR(Bool, fua_psprite_check)
EXTERN_CVAR(Bool, fua_shading_check)

namespace zx { namespace hwrender {

// [rc4l] Where this level's set-up stands, as levelsetup_compute steps it.
static LevelSetup g_setup = ComputeLevelSetupStart();

// [rc4l] Bake this level from the map and upload it, once, without anyone typing anything.

static bool AutoSetupForLevel()
{
	ComputeLevelSetupForLevel(g_setup, zx::levelmesh::LevelGeneration());

	switch (g_setup.state)
	{
	case kSetupArm:
		if (gamestate != GS_LEVEL) return false;
		// [rc4l] Drop everything keyed on a pointer the old level owned.
		//
		// The texture cache and the material SRBs are keyed on raw FMaterial*, and ReleaseMaterials
		// was never called from anywhere -- so they survived every map change for the life of the
		// process. That is fine until the engine frees a level's materials and the allocator hands
		// the same addresses to a new level's, at which point the cache serves the PREVIOUS map's
		// texture for a material that merely happens to live where the old one did. It needs several
		// map changes to show up, which is exactly how it was found: a map that renders correctly on
		// its own came up with black and wrong surfaces after cycling through the rotation to reach
		// it. SRBs reference the textures, so they go first.
		ReleaseBatchSRBs();
		ReleaseMaterialSRBs();
		for (unsigned int b = 0; b < g_scene.batches.Size(); b++) g_scene.batches[b].srb = NULL;
		// [rc4l] The material SLOT TABLE holds the same stale pointers, and it is worse than the caches
		// because something reads it every frame.
		//
		// Between a level change and the new scene upload, RefreshBindless would fill the array from
		// the old level's FMaterial pointers -- and GetMaterialSRV caches what it makes of them, keyed
		// on those same addresses. The new level then allocates its materials at the addresses the old
		// ones just vacated, hits the poisoned cache, and every surface comes up wearing some other
		// surface's texture. 93% of the frame on dbab04, and only ever after a map change, which is why
		// it looked like the map rather than the transition.
		ResetMaterialSlots();
		g_stats.dynSlotUnfilledTotal = 0;
		ReleaseWorldSRBs();
		// [rc4l] And the PIPELINES, because the material array lives on them.
		//
		// uMaterials is a STATIC variable, which means Diligent stores it on the pipeline and copies it
		// into every binding made from that pipeline. A level change refills it -- and on a pipeline
		// that has already handed out bindings, refilling does not take: the array keeps the PREVIOUS
		// level's textures, so the new level renders with its geometry perfect and every surface
		// wearing the wrong picture. A map loaded directly is fine because the array is filled before
		// any binding exists, which is why this only ever showed up after a map change.
		ReleaseScenePipelines();
		ReleaseMaterials();
		// Camera targets are keyed on FMaterial* too, and go stale for exactly the same reason.
		ReleaseCameraTargets();
		ReleaseAccelerationStructures();
		CollectMirrors();
		ResetSkyForLevel();
		// [rc4l] The switch implies the level mesh. gl_wallmesh is off by default, so the first
		// self-arming run baked nothing and reported "no baked geometry -- set gl_wallmesh 1, walk
		// the level, then retry" -- advice aimed at a person typing commands, from a path whose whole
		// point is that nobody is.
		if (!gl_wallmesh) gl_wallmesh = true;
		{
			// [rc4l] Every wall and plane straight from the map, so the level is baked whole before its first view.
			const int built = zx::levelmesh::BakeLevelFromMap();
			const int flats = zx::levelmesh::BakeFlatsFromMap();
			if (built > 0 || flats > 0)
				Printf("vulkan: %d wall parts and %d planes built from the map\n", built, flats);
		}
		ComputeLevelSetupBaked(g_setup);
		return false;
	case kSetupBaked:
		// [rc4l] Upload the level the map baked whole, retrying a failed upload on later frames, since giving up is permanent for the level.
		{
			FString report;
			ComputeLevelSetupUploaded(g_setup, SceneUpload(report));
			if (g_setup.state == kSetupReady) Printf("vulkan: %s\n", report.GetChars());
			else if (g_setup.state == kSetupGaveUp) Printf("vulkan: setup failed after %d attempts -- %s\n", g_setup.tries, report.GetChars());
		}
		return g_setup.state == kSetupReady;
	case kSetupReady:
		return true;
	default:
		return false;
	}
}

// [rc4l] Read by dgwin32.cpp when it creates the window; see fua_dg_embed.
bool Fua_WantEmbeddedWindow() { return !!fua_dg_embed; }

// [rc4l] THE FRAME, which nothing else in this backend measures.
//
// Every clock in this file and in stat rendertimes is a COMPONENT: All= is GL's render, the backend
// phases are the passes, fua_diligent_bench re-renders the resident mesh in a loop without running a
// game frame at all. Each of them can read 0.7 ms while the player is looking at 146 fps, because
// none of them measures the interval between one picture appearing and the next -- which is the only
// number a player can feel, and the only one that shows a stutter.
//
// So: wall clock between successive presents, kept as a ring, reported as percentiles by
// fua_frametimes. A stutter is a p99 far above the median, and no average will ever show it.
namespace {
const int kFrameRing = 1024;
double  g_frameMs[kFrameRing];
int     g_frameCount = 0;
int     g_frameHead  = 0;
bool    g_frameHavePrev = false;
std::chrono::steady_clock::time_point g_framePrev;
}

// [rc4l] What each recorded frame was made of, in step with g_frameMs.
//
// A percentile on its own says a frame was slow and nothing about why, and every attribution in this
// work that was reasoned rather than measured turned out wrong. These ride the same ring as the
// interval so the slow frames can be asked directly what they were carrying.
struct FrameParts { float invalMs; int sectors; int repaints; int runs; int patchVerts; bool ticked; };
static FrameParts g_frameParts[kFrameRing];
static int g_lastLevelTime = -1;

void RecordPresentInterval()
{
	const auto now = std::chrono::steady_clock::now();
	if (g_frameHavePrev)
	{
		const double ms = std::chrono::duration<double, std::milli>(now - g_framePrev).count();
		g_frameMs[g_frameHead] = ms;
		FrameParts &fp = g_frameParts[g_frameHead];
		fp.invalMs    = (float)zx::levelmesh::InvalidationMsLastFrame();
		fp.sectors    = zx::levelmesh::SectorsDirtyLastFrame();
		fp.repaints   = g_stats.fRepaints;
		fp.runs       = g_stats.fRuns;
		fp.patchVerts = g_stats.fPatchVerts;
		fp.ticked     = (level.time != g_lastLevelTime);
		g_lastLevelTime = level.time;
		g_stats.fRepaints = g_stats.fRuns = g_stats.fPatchVerts = g_stats.fPatches = 0;
		g_frameHead = (g_frameHead + 1) % kFrameRing;
		if (g_frameCount < kFrameRing) g_frameCount++;
	}
	g_framePrev = now;
	g_frameHavePrev = true;
}

void ResetFrameTimes() { g_frameCount = 0; g_frameHead = 0; g_frameHavePrev = false; }

CCMD( fua_frametimes )
{
	if (g_frameCount < 8) { Printf( "fua_frametimes: not enough frames yet -- play for a second.\n" ); return; }
	// [rc4l] The bridge's frame-time summary, so this and perf.capture agree on what a percentile is.
	const zx::mcp::PerfSummary s = zx::mcp::SummarizeFrameTimes(std::vector<double>(g_frameMs, g_frameMs + g_frameCount));
	const double med = s.p50, p99 = s.p99;
	// How many frames took more than twice the median -- that is what a stutter IS.
	int hitches = 0; for (int i = 0; i < g_frameCount; i++) if (g_frameMs[i] > med * 2.0) hitches++;
	Printf( "fua_frametimes over %d frames: min %.2f  med %.2f  mean %.2f  p95 %.2f  p99 %.2f  max %.2f ms\n",
		g_frameCount, s.min, med, s.mean, s.p95, p99, s.max );
	Printf( "  %.0f fps at the median, %.0f at p99   |   %d frames (%.1f%%) took over twice the median\n",
		1000.0 / med, 1000.0 / p99, hitches, 100.0 * hitches / g_frameCount );
	// [rc4l] WHAT the slow frames were carrying, next to what a median frame carries.
	//
	// A percentile says a frame was slow and nothing about why. Every attribution in this work that
	// was reasoned instead of measured turned out to be wrong -- the stutter was blamed on moving
	// geometry when three quarters of it was flickering lights. So the components ride the same ring
	// as the interval, and the slow frames are asked directly.
	{
		double slowMs = 0, medMs = 0;
		double sInval = 0, mInval = 0;
		double sRep = 0, mRep = 0, sRuns = 0, mRuns = 0, sPatch = 0, mPatch = 0, sSect = 0, mSect = 0;
		int nSlow = 0, nMed = 0, slowTicked = 0, medTicked = 0;
		for (int i = 0; i < g_frameCount; i++)
		{
			const FrameParts &fp = g_frameParts[i];
			const bool slow = g_frameMs[i] >= p99;
			double &ms = slow ? slowMs : medMs; ms += g_frameMs[i];
			(slow ? sInval : mInval) += fp.invalMs;
			(slow ? sRep   : mRep)   += fp.repaints;
			(slow ? sRuns  : mRuns)  += fp.runs;
			(slow ? sPatch : mPatch) += fp.patchVerts;
			(slow ? sSect  : mSect)  += fp.sectors;
			if (fp.ticked) { if (slow) slowTicked++; else medTicked++; }
			if (slow) nSlow++; else nMed++;
		}
		// [rc4l] SAY SO when the sim was frozen, because the numbers stop meaning frame rate.
		//
		// A paused engine presents once per tic, so every reading collapses to ~28.6 ms / 35 fps
		// however fast the renderer actually is. rc4l reported "sub 300" at a vantage that measures
		// 305 fps running and 37 fps paused -- the menu was open. Reading a frame time off a frozen
		// sim has now misled this work twice, so the report refuses to be quoted out of context.
		if (slowTicked + medTicked == 0 && g_frameCount > 32)
			Printf( "  NOTE: no game tic ran during this sample -- the sim was paused or the window "
			        "was inactive.\n        A frozen sim presents once per tic, so this is the 35 Hz tic "
			        "rate, not the renderer's speed.\n" );
		if (nSlow > 0 && nMed > 0)
		{
			Printf( "  what a frame carries      %-12s %-12s\n", "p99+", "the rest" );
			Printf( "    frames                  %-12d %-12d\n", nSlow, nMed );
			Printf( "    mean frame time         %-12.2f %-12.2f ms\n", slowMs/nSlow, medMs/nMed );
			Printf( "    ran a game tic          %-11.0f%% %-11.0f%%\n",
				100.0*slowTicked/nSlow, 100.0*medTicked/nMed );
			Printf( "    sectors invalidated     %-12.1f %-12.1f\n", sSect/nSlow, mSect/nMed );
			Printf( "    invalidation loop       %-12.3f %-12.3f ms\n", sInval/nSlow, mInval/nMed );
			Printf( "    surfaces repainted      %-12.1f %-12.1f\n", sRep/nSlow, mRep/nMed );
			Printf( "    repaint uploads         %-12.1f %-12.1f\n", sRuns/nSlow, mRuns/nMed );
			Printf( "    vertices patched        %-12.1f %-12.1f\n", sPatch/nSlow, mPatch/nMed );
			Printf( "    unattributed            %-12.3f %-12.3f ms  (tic, draw, present)\n",
				slowMs/nSlow - sInval/nSlow, medMs/nMed - mInval/nMed );
		}
	}

	// [rc4l] The SEQUENCE, not just the percentiles. A stutter has a shape: every-Nth-frame is a
	// timer, bursts are a stall, and alternating is a double-buffer fighting something. Percentiles
	// cannot tell those apart and the raw run can.
	if (argv.argc() > 1)
	{
		FString line;
		const int n = (g_frameCount < 48) ? g_frameCount : 48;
		const int start = (g_frameHead - n + kFrameRing) % kFrameRing;
		for (int i = 0; i < n; i++)
		{
			line.AppendFormat( "%.1f ", g_frameMs[(start + i) % kFrameRing] );
			if ((i % 12) == 11) { Printf( "  %s\n", line.GetChars() ); line = ""; }
		}
		if (line.Len()) Printf( "  %s\n", line.GetChars() );
	}
	ResetFrameTimes();
}

// [rc4l] See hud2d.h; it does not ask about fua_dg_standalone, since with the embedded window up GL's output is discarded either way.
bool BackendPresenting()
{
	if (!fua_vulkan || !fua_dg_embed) return false;
	return GetSwapChain() != NULL;
}

// [rc4l] Whether the backend's scene is up: switched on, the level baked and geometry there, GL keeping the frame otherwise rather than leaving a black screen.
bool StandaloneActive()
{
	return fua_vulkan && g_setup.state == kSetupReady && SceneDrawable();
}

zx::frame::FrameRoute FrameRouteNow(bool hasGL, bool hasBackend)
{
	zx::frame::FrameRouteIn in;
	in.hasGL = hasGL;
	in.hasBackend = hasBackend;
	in.backendOn = BackendOnly() || fua_vulkan;
	in.sceneUp = StandaloneActive();
	in.backendPresenting = BackendPresenting();
	in.standalone = fua_dg_standalone;
	in.embedded = fua_dg_embed;
	in.checking = fua_vis_check || fua_sprite_check || fua_psprite_check || fua_shading_check;
	return zx::frame::ComputeFrameRoute(in);
}

// [rc4l] Steps this level's scene set-up while fua_vulkan is on, true once it can be drawn.
static bool AdvanceLevelScene()
{
	return fua_vulkan ? AutoSetupForLevel() : false;
}

void SettleLevelScene()
{
	// [rc4l] Baking and uploading each take a step, so a few settle this level's set-up whatever the last level left.
	for (int i = 0; i < 4; i++)
		if (AdvanceLevelScene() && StandaloneActive()) return;
}

// [rc4l] One live frame from the current camera, its engine-side declaration in dglive.h.
void LiveFrame()
{
	// [rc4l] A launch without GL presents from its frame buffer's Update, where every path of D_Display passes.
	if (BackendOnly()) return;
	zx::hwrender::GLFrameDone();
	const bool autoReady = AdvanceLevelScene();

	// Uncover the GL frame underneath when the backend is off, rather than leaving the last Vulkan
	// frame frozen over it. This is what makes turning it off an A/B toggle you can hold.
	if (!autoReady && !fua_diligent_live)
	{
		Fua_ShowBackendWindow(GetBackendWindow(), 0);
		return;
	}
	Fua_ShowBackendWindow(GetBackendWindow(), 1);
	if (!SceneDrawable()) return;
	if (GetBackendWindow() == NULL) return;

	// The camera is rebuilt every frame -- that is the entire point of this path. Everything else
	// (geometry, materials, per-vertex light) is already resident, so a live frame is a matrix
	// upload and the batch loop.
	BuildMVP(g_mvp);
	DrawSceneOnce(true, true);
	zx::hwrender::BackendFrameDone();
}

// [rc4l] Whether the engine drew a view of the world this frame, so a launch without GL draws the world only under one.
static bool g_worldViewThisFrame = false;

void NoteWorldView() { g_worldViewThisFrame = true; }

bool OpenBackendOutput(int width, int height)
{
	Fua_SetBackendWindowSize(width, height);
	FString err;
	if (!DiligentShowWindow(err))
	{
		Printf("-vulkan: %s\n", err.GetChars());
		return false;
	}
	Fua_ShowBackendWindow(GetBackendWindow(), 1);
	return true;
}

int BackendMaxTextureSize()
{
	auto *dev = GetDevice();
	return dev != NULL ? (int)dev->GetAdapterInfo().Texture.MaxTexture2DDimension : 0;
}

// [rc4l] The 2D layer alone over black: the console, the menus, the title and the intermission, with no world under them.
static void DrawOverlay(bool present)
{
	auto *ctx = GetContext();
	auto *swap = GetSwapChain();
	if (!ctx || !swap) return;
	const auto &sd = swap->GetDesc();
	if (sd.Width == 0 || sd.Height == 0) return;
	Diligent::ITextureView *rtv = FrameRTV();
	ctx->SetRenderTargets(1, &rtv, NULL, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	// [rc4l] A wipe under way is the picture under the console and menus.
	if (WipeActive()) WipeDraw(ctx);
	else
	{
		const float black[4] = { 0.f, 0.f, 0.f, 1.f };
		ctx->ClearRenderTarget(rtv, black, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}
	Draw2D(ctx);
	if (present) PresentFrame(ctx, swap, true);
}

// [rc4l] Whether the last frame drew the world under its 2D, so a screenshot redraws what was shown.
static bool g_lastFrameWorld = false;

static void DrawBackendFrame(bool present)
{
	if (g_lastFrameWorld)
	{
		BuildMVP(g_mvp);
		DrawSceneOnce(present, true);
	}
	else DrawOverlay(present);
}

void WipeEnd()
{
	// [rc4l] The frame the wipe goes to, drawn without a present as the next Update would draw it, then its 2D dropped: the console and menus over the wipe come afresh each tic.
	const bool ready = AdvanceLevelScene();
	g_lastFrameWorld = g_worldViewThisFrame && ready && SceneDrawable();
	g_worldViewThisFrame = false;
	DrawBackendFrame(false);
	WipeTakeEnd();
	zx::hwrender::Clear2D();
}

void PresentBackendOnly(bool present)
{
	const bool ready = AdvanceLevelScene();
	const bool world = g_worldViewThisFrame && ready && SceneDrawable();
	g_worldViewThisFrame = false;
	if (!present) return;
	zx::hwrender::GLFrameDone();
	g_lastFrameWorld = world;
	DrawBackendFrame(true);
	zx::hwrender::BackendFrameDone();
}

unsigned char *ReadBackendFrame(int &width, int &height)
{
	DrawBackendFrame(false);
	int format = 0;
	bool bgra = false;
	FString report;
	unsigned char *rgb = ReadBackBuffer(width, height, format, bgra, report);
	if (rgb == NULL) Printf("screenshot: %s\n", report.GetChars());
	return rgb;
}

}} // namespace zx::hwrender
