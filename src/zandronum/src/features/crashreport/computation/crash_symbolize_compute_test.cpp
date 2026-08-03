// [rc4l] Tests for the crash title/fingerprint decisions. Pins the two real cases: an Apple GL
// driver crash on a worker thread (all system frames, no app frames -> title is the driver
// function, never "<unknown>") and an in-app crash (title is the deepest ZandroX frame).
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include "features/crashreport/computation/crash_symbolize_compute.h"

using namespace zx::crashreport;

static ResolvedFrame Sys(const char *f)  { ResolvedFrame r; r.func = f; r.mainModule = false; return r; }
static ResolvedFrame App(const char *f)  { ResolvedFrame r; r.func = f; r.mainModule = true;  return r; }

// ---- NormalizeFrameName ----------------------------------------------------

TEST(CrashSymbolize, NormalizeStripsBacktraceOffset)
{
	EXPECT_EQ(NormalizeFrameName("glgProcessColor + 3024"), "glgProcessColor");
	EXPECT_EQ(NormalizeFrameName("AActor::Tick() + 12"), "AActor::Tick()");
	EXPECT_EQ(NormalizeFrameName("func + 0x8"), "func");           // hex offset
}

TEST(CrashSymbolize, NormalizeTrimsAndLeavesCleanNames)
{
	EXPECT_EQ(NormalizeFrameName("  _pthread_wqthread  "), "_pthread_wqthread");
	EXPECT_EQ(NormalizeFrameName("A_MonsterProjectile"), "A_MonsterProjectile"); // no offset -> unchanged
	EXPECT_EQ(NormalizeFrameName(""), "");
}

TEST(CrashSymbolize, NormalizeKeepsAPlusThatIsNotAnOffset)
{
	// A "+" followed by a non-number (e.g. operator+, or a mangled tail) must NOT be stripped.
	EXPECT_EQ(NormalizeFrameName("operator + overload"), "operator + overload");
}

TEST(CrashSymbolize, NormalizeReTrimsWhitespaceLeftAfterStrippingOffset)
{
	// Stripping the " + <n>" can leave trailing space that must be re-trimmed.
	EXPECT_EQ(NormalizeFrameName("sym  + 5"), "sym");
	EXPECT_EQ(NormalizeFrameName("   only spaces   "), "only spaces"); // pure trim, both ends
	EXPECT_EQ(NormalizeFrameName("weird + "), "weird +");             // empty offset tail -> not stripped
}

// ---- ComputeCrashIdentity: the Apple GL driver crash (no app frames) -------

TEST(CrashSymbolize, DriverCrashOnWorkerThreadIsNamedNotUnknown)
{
	// Real windowed<->fullscreen crash: all system frames, deepest is Apple's GL driver.
	std::vector<ResolvedFrame> frames = {           // outermost-first (as sentry stores)
		Sys("start_wqthread"),
		Sys("_pthread_wqthread + 232"),
		Sys("_dispatch_worker_thread2 + 156"),
		Sys("_dispatch_continuation_pop + 728"),
		Sys("_dispatch_client_callout + 16"),
		Sys("_dispatch_call_block_and_release + 32"),
		Sys("__glgProcessPixelsWithProcessor_block_invoke + 144"),
		Sys("glgProcessColor + 3024"),              // deepest == crash site
	};
	CrashIdentity id = ComputeCrashIdentity(frames);
	EXPECT_EQ(id.title, "glgProcessColor");                 // named, NOT "<unknown>"
	EXPECT_NE(id.title.find("unknown"), 0u);                // never leads with unknown
	// Fingerprint is the innermost few names -> deterministic and distinct per site.
	EXPECT_EQ(id.fingerprint,
		"glgProcessColor;__glgProcessPixelsWithProcessor_block_invoke;"
		"_dispatch_call_block_and_release;_dispatch_client_callout;_dispatch_continuation_pop");
}

// ---- ComputeCrashIdentity: an in-app crash ---------------------------------

TEST(CrashSymbolize, InAppCrashPicksDeepestMainModuleFrame)
{
	std::vector<ResolvedFrame> frames = {
		App("main"),
		App("D_DoomLoop()"),
		App("AActor::Tick() + 40"),
		App("AInventory::CallTryPickup(AActor*, AActor**) + 6"),   // deepest app frame == crash site
	};
	CrashIdentity id = ComputeCrashIdentity(frames);
	EXPECT_EQ(id.title, "AInventory::CallTryPickup(AActor*, AActor**)");
}

TEST(CrashSymbolize, AppCallingSystemCrashesBlamesTheAppCaller)
{
	// App calls into libc which faults: the deepest frame is system (memmove), but the title should
	// be the app code responsible, not the libc primitive.
	std::vector<ResolvedFrame> frames = {
		App("P_LoadThings(int)"),
		App("SpawnMapThing() + 8"),
		Sys("_platform_memmove + 100"),   // deepest, but system
	};
	CrashIdentity id = ComputeCrashIdentity(frames);
	EXPECT_EQ(id.title, "SpawnMapThing()");   // deepest MAIN-MODULE frame wins the title
}

// ---- grouping behaviour ----------------------------------------------------

TEST(CrashSymbolize, SameSiteSameFingerprint_DifferentSiteDiffers)
{
	std::vector<ResolvedFrame> a = { Sys("caller"), App("crashHere + 4") };
	std::vector<ResolvedFrame> b = { Sys("caller"), App("crashHere + 999") }; // same site, diff offset
	std::vector<ResolvedFrame> c = { Sys("caller"), App("otherCrash") };      // different site
	EXPECT_EQ(ComputeCrashIdentity(a).fingerprint, ComputeCrashIdentity(b).fingerprint);
	EXPECT_NE(ComputeCrashIdentity(a).fingerprint, ComputeCrashIdentity(c).fingerprint);
}

// ---- fully-unresolved fallback (never crashes, never "<unknown>") ----------

TEST(CrashSymbolize, NothingResolvedFallsBackToStableLabel)
{
	std::vector<ResolvedFrame> frames = { Sys(""), Sys(""), Sys("") };
	CrashIdentity id = ComputeCrashIdentity(frames);
	EXPECT_EQ(id.title, "crash (3 frames, no symbols)");
	EXPECT_EQ(id.fingerprint, "nosym:3");
}

TEST(CrashSymbolize, EmptyStackIsHandled)
{
	CrashIdentity id = ComputeCrashIdentity({});
	EXPECT_EQ(id.title, "crash (0 frames, no symbols)");
	EXPECT_EQ(id.fingerprint, "nosym:0");
}
