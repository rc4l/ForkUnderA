// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See mcp_sample.h for why the engine samples itself on Windows. The counting and ranking
// live in features/mcp-bridge/computation/sampleagg_compute; this file is only the part that has to
// touch thread handles and dbghelp.

#include "mcp_sample.h"
#include "mcp_bridge.h"   // [rc4l] MCP_Crash_GameThreadForSampling

#ifdef FUA_MCP_BRIDGE

#include "features/mcp-bridge/computation/sampleagg_compute.h"

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
  #include <windows.h>
  #include <dbghelp.h>
  #pragma comment(lib, "dbghelp.lib")   // MSVC-only; ignored by GCC/Clang
  #include "features/telemetry/osprobe.h"   // [rc4l] DbgHelpLock
#endif

namespace
{

// [rc4l] The sampler thread touches NOTHING of the engine: no globals, no cvars, no Printf, and no
// FString (its default constructor bumps a non-atomic refcount on a shared NullString). Plain
// std::string and the compute unit only. The finished report crosses back through this mutex and is
// picked up by the main thread in MCP_Sample_ReportReady.
std::mutex g_lock;
std::string g_report;
bool g_ready = false;
bool g_running = false;

// [rc4l] The main thread's leveltime, published one way for the sampler to stamp onto each sample, since the sampler may not read level.time itself.
std::atomic<int> g_publishedTic( 0 );

#ifdef _WIN32

// [rc4l] Symbolising waits until the run ends, since resolving a symbol can take a page fault and a profiler holding its suspended target across one deadlocks it.
std::string SymbolFor( HANDLE proc, DWORD64 addr, std::string &dsoOut )
{
	char symbuf[sizeof( SYMBOL_INFO ) + 512];
	SYMBOL_INFO *sym = (SYMBOL_INFO *)symbuf;
	sym->SizeOfStruct = sizeof( SYMBOL_INFO );
	sym->MaxNameLen = 511;

	IMAGEHLP_MODULE64 mod;
	memset( &mod, 0, sizeof( mod ));
	mod.SizeOfStruct = sizeof( mod );
	if ( SymGetModuleInfo64( proc, addr, &mod ))
		dsoOut = mod.ModuleName;

	DWORD64 disp = 0;
	if ( SymFromAddr( proc, addr, &disp, sym ))
		return std::string( sym->Name );

	return std::string( );
}

// [rc4l] An address's name and module, looked up once however many samples land on it.
const zx::SampleHit &Resolve( HANDLE proc, std::map<DWORD64, zx::SampleHit> &resolved, DWORD64 addr )
{
	std::map<DWORD64, zx::SampleHit>::iterator at = resolved.find( addr );
	if ( at == resolved.end( ))
	{
		std::string dso;
		const std::string symbol = SymbolFor( proc, addr, dso );
		at = resolved.insert( std::make_pair( addr, zx::SampleHit( symbol, dso ))).first;
	}
	return at->second;
}

// [rc4l] The deepest stack a sample keeps, enough to reach D_Display from the walk's leaves.
const unsigned kMaxDepth = 48;

// [rc4l] The suspended thread's frames from its context by each image's unwind data, innermost first, kept inside [ctx.Rsp, stackHigh) and with no allocation or dbghelp while it is held.
unsigned WalkStack( CONTEXT ctx, DWORD64 stackHigh, std::vector<DWORD64> &frames )
{
	unsigned n = 0;
#if defined( _M_X64 )
	const DWORD64 stackLow = ctx.Rsp;
	while (( n < kMaxDepth ) && ( ctx.Rip != 0 ) && ( ctx.Rsp >= stackLow ) && ( ctx.Rsp + 8 <= stackHigh ))
	{
		frames.push_back( ctx.Rip );
		n++;
		DWORD64 imageBase = 0;
		PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry( ctx.Rip, &imageBase, NULL );
		if ( fn == NULL )
		{
			// [rc4l] A leaf keeps no unwind data, and its return address is on top of its stack.
			ctx.Rip = *(const DWORD64 *)ctx.Rsp;
			ctx.Rsp += 8;
		}
		else
		{
			PVOID handlerData = NULL;
			DWORD64 establisher = 0;
			RtlVirtualUnwind( UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx, &handlerData, &establisher, NULL );
		}
	}
#else
	(void)ctx; (void)stackHigh; (void)frames;
#endif
	return n;
}

// One run: interrupt `thread` at `hz`, then resolve what was collected. Owns `thread` and closes it.
void SampleRun( HANDLE thread, double seconds, int hz, bool engineOnly, int top, int ticMin, int ticMax,
	bool stacks, DWORD64 stackHigh )
{
	std::vector<DWORD64> pcs;
	std::vector<int> tics;					// the leveltime each sample landed in, parallel to pcs
	std::vector<DWORD64> frames;			// [rc4l] every sample's stack, back to back
	std::vector<unsigned> depths;			// [rc4l] how many of those frames each sample has, parallel to pcs

	const int intervalUs = ( hz > 0 ) ? ( 1000000 / hz ) : 1000;

	// [rc4l] A high-resolution waitable timer rather than Sleep. Sleep's granularity is the system
	// timer tick, which is ~15.6 ms unless some process has globally lowered it -- that would be ~64
	// samples a second instead of the requested rate, and a 2 s run would rank 128 samples and call
	// it a profile. This asks for fine granularity for THIS timer only, so nothing else on the
	// machine has its timing changed. Falls back to Sleep where the flag is not supported.
	HANDLE timer = CreateWaitableTimerExW( NULL, NULL,
		CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS );
	if ( timer == NULL )
		timer = CreateWaitableTimerExW( NULL, NULL, 0, TIMER_ALL_ACCESS );

	LARGE_INTEGER freq;
	QueryPerformanceFrequency( &freq );
	LARGE_INTEGER start;
	QueryPerformanceCounter( &start );

	const double budget = ( seconds > 0.0 ) ? seconds : 1.0;
	double elapsed = 0.0;

	// [rc4l] Room for every sample up front, since growing a vector while the main thread is suspended can wait on a heap lock that thread holds.
	const size_t capacity = (size_t)( budget * ( hz > 0 ? hz : 1000 ) * 1.25 ) + 64;
	pcs.reserve( capacity );
	tics.reserve( capacity );
	if ( stacks )
	{
		frames.reserve( capacity * kMaxDepth );
		depths.reserve( capacity );
	}

	while (( elapsed < budget ) && ( pcs.size( ) < capacity ))
	{
		// Suspend, read the instruction pointer, resume. Nothing else happens in here: every
		// microsecond between these two calls is a microsecond the game is not running, so the
		// profiler would be measuring itself.
		if ( SuspendThread( thread ) != (DWORD)-1 )
		{
			CONTEXT ctx;
			memset( &ctx, 0, sizeof( ctx ));
			// [rc4l] Unwinding restores the non-volatile registers, so a stack walk needs them too.
			ctx.ContextFlags = stacks ? CONTEXT_FULL : CONTEXT_CONTROL;

			if ( GetThreadContext( thread, &ctx ))
			{
#if defined( _M_X64 ) || defined( __x86_64__ )
				pcs.push_back( (DWORD64)ctx.Rip );
#elif defined( _M_ARM64 )
				pcs.push_back( (DWORD64)ctx.Pc );
#else
				pcs.push_back( (DWORD64)ctx.Eip );
#endif
				tics.push_back( g_publishedTic.load( std::memory_order_relaxed ));
				if ( stacks ) depths.push_back( WalkStack( ctx, stackHigh, frames ));
			}

			ResumeThread( thread );
		}

		if ( timer != NULL )
		{
			LARGE_INTEGER due;
			due.QuadPart = -( (LONGLONG)intervalUs * 10 );	// relative, 100 ns units
			SetWaitableTimer( timer, &due, 0, NULL, NULL, FALSE );
			WaitForSingleObject( timer, INFINITE );
		}
		else
		{
			Sleep( intervalUs / 1000 > 0 ? intervalUs / 1000 : 1 );
		}

		LARGE_INTEGER now;
		QueryPerformanceCounter( &now );
		elapsed = (double)( now.QuadPart - start.QuadPart ) / (double)freq.QuadPart;
	}

	if ( timer != NULL )
		CloseHandle( timer );

	// The main thread runs freely again from here; everything below is bookkeeping.
	// [rc4l] dbghelp is one per process and not thread-safe, so the session holds the lock features/telemetry's SymbolNames takes on the main thread.
	std::unique_lock<std::mutex> symbols( zx::telemetry::DbgHelpLock( ));
	HANDLE proc = GetCurrentProcess( );
	SymSetOptions( SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS );
	SymInitialize( proc, NULL, TRUE );

	// Resolve each DISTINCT address once. A hot function is by definition sampled many times at the
	// same few addresses, so this turns tens of thousands of lookups into a few hundred.
	std::map<DWORD64, zx::SampleHit> resolved;
	std::vector<zx::SampleHit> hits;
	hits.reserve( pcs.size( ));

	// [rc4l] The tic window, applied BEFORE symbolising so a narrow window is also cheap to resolve.
	// 0,0 means the whole run. This is the point of an in-engine sampler: ask perf.ticprof which
	// tics were expensive, then ask for exactly those, instead of averaging a spike into the calm
	// either side of it and being told the wrong answer.
	// [rc4l] Each kept sample's place, its stack's frames named through the same table, for the inclusive ranking.
	std::vector<std::vector<zx::SampleHit> > stackHits;
	size_t frameAt = 0;
	for ( size_t i = 0; i < pcs.size( ); ++i )
	{
		const unsigned depth = ( i < depths.size( )) ? depths[i] : 0;
		const size_t first = frameAt;
		frameAt += depth;
		if ( !zx::ComputeInTicWindow(( i < tics.size( )) ? tics[i] : 0, ticMin, ticMax )) continue;

		hits.push_back( Resolve( proc, resolved, pcs[i] ));
		if ( !stacks ) continue;
		std::vector<zx::SampleHit> stack;
		for ( unsigned k = 0; k < depth; ++k ) stack.push_back( Resolve( proc, resolved, frames[first + k] ));
		stackHits.push_back( stack );
	}

	SymCleanup( proc );
	symbols.unlock( );

	// ModuleName has no extension, so the engine is "forkundera" whatever the binary is called.
	if ( engineOnly )
		hits = zx::OnlyFrom( hits, "forkundera" );

	const size_t rows = ( top > 0 ) ? (size_t)top : 0;
	const std::string json = zx::SampleReportJson( zx::RankSamples( hits, rows ), zx::ComputeInclusiveRanks( stackHits, rows ),
		elapsed, (unsigned long long)hits.size( ));

	CloseHandle( thread );

	std::lock_guard<std::mutex> guard( g_lock );
	g_report = json;
	g_ready = true;
	g_running = false;
}

#endif // _WIN32

} // namespace

bool MCP_Sample_Arm( double seconds, int hz, bool engineOnly, int top, int ticMin, int ticMax, bool stacks, bool gameThread )
{
#ifdef _WIN32
	{
		std::lock_guard<std::mutex> guard( g_lock );
		if ( g_running )
			return false;

		g_running = true;
		g_ready = false;
	}

	// A real handle for the CALLING thread, which is the main thread: GetCurrentThread returns a
	// pseudo-handle that means "whoever is asking", so handing it to another thread would have the
	// sampler suspend ITSELF. The duplicate is closed by the run.
	HANDLE thread = NULL;
	ULONG_PTR stackLow = 0, stackHigh = 0;
	// [rc4l] The game thread's handle and stack top come from the crash module, which took them on that thread.
	void *game = NULL;
	unsigned long long gameStackHigh = 0;
	const bool armed = gameThread
		? MCP_Crash_GameThreadForSampling( &game, &gameStackHigh )
		: DuplicateHandle( GetCurrentProcess( ), GetCurrentThread( ), GetCurrentProcess( ), &thread,
			THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0 ) != FALSE;
	if ( !armed )
	{
		std::lock_guard<std::mutex> guard( g_lock );
		g_running = false;
		return false;
	}
	if ( gameThread )
	{
		thread = (HANDLE)game;
		stackHigh = (ULONG_PTR)gameStackHigh;
	}
	else
	{
		// [rc4l] The top of the main thread's stack, read here on that thread, which bounds every walk of it.
		GetCurrentThreadStackLimits( &stackLow, &stackHigh );
	}

	std::thread( SampleRun, thread, seconds, hz, engineOnly, top, ticMin, ticMax, stacks, (DWORD64)stackHigh ).detach( );
	return true;
#else
	// mac and Linux keep the external samplers, which walk the whole stack and see every thread.
	(void)seconds; (void)hz; (void)engineOnly; (void)top; (void)ticMin; (void)ticMax; (void)stacks; (void)gameThread;
	return false;
#endif
}

bool MCP_Sample_ReportReady( std::string &json )
{
	std::lock_guard<std::mutex> guard( g_lock );
	if ( !g_ready )
		return false;

	json = g_report;
	g_ready = false;
	g_report.clear( );
	return true;
}

void MCP_Sample_PublishTic( int leveltime )
{
	g_publishedTic.store( leveltime, std::memory_order_relaxed );
}

bool MCP_Sample_Running( )
{
	std::lock_guard<std::mutex> guard( g_lock );
	return g_running;
}

#endif // FUA_MCP_BRIDGE
