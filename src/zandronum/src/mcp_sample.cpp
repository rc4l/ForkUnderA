// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See mcp_sample.h for why the engine samples itself on Windows. The counting and ranking
// live in features/mcp-bridge/computation/sampleagg_compute; this file is only the part that has to
// touch thread handles and dbghelp.

#include "mcp_sample.h"

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
#endif

namespace
{

// [rc4l] The sampler thread touches NOTHING of the engine: no globals, no cvars, no Printf, and no
// FString (its default constructor bumps a non-atomic refcount on a shared NullString). Plain
// std::string and the compute unit only. The finished report crosses back through this mutex and is
// picked up by the game thread in MCP_Sample_ReportReady.
std::mutex g_lock;
std::string g_report;
bool g_ready = false;
bool g_running = false;

// [rc4l] The game thread's leveltime, published for the sampler to stamp onto each sample. Atomic
// and one-way: the worker reads, never writes, and never touches level.time itself -- reading an
// engine global from another thread is exactly what the bridge's threading contract forbids.
std::atomic<int> g_publishedTic( 0 );

#ifdef _WIN32

// [rc4l] dbghelp is single-threaded per process. mcp_crash.cpp is the only other user and it runs
// from the unhandled-exception filter, at which point nobody is sampling any more, so the two cannot
// overlap in practice. Symbolising is done AFTER the run, never while the game thread is suspended:
// resolving a symbol can take a page fault, and holding a suspended thread across one is how a
// profiler deadlocks its own target.
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

// One run: interrupt `thread` at `hz`, then resolve what was collected. Owns `thread` and closes it.
void SampleRun( HANDLE thread, double seconds, int hz, bool engineOnly, int top, int ticMin, int ticMax )
{
	std::vector<DWORD64> pcs;
	std::vector<int> tics;					// the leveltime each sample landed in, parallel to pcs

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

	while ( elapsed < budget )
	{
		// Suspend, read the instruction pointer, resume. Nothing else happens in here: every
		// microsecond between these two calls is a microsecond the game is not running, so the
		// profiler would be measuring itself.
		if ( SuspendThread( thread ) != (DWORD)-1 )
		{
			CONTEXT ctx;
			memset( &ctx, 0, sizeof( ctx ));
			ctx.ContextFlags = CONTEXT_CONTROL;

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

	// The game thread runs freely again from here; everything below is bookkeeping.
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
	const bool bWindowed = ( ticMin > 0 ) || ( ticMax > 0 );

	for ( size_t i = 0; i < pcs.size( ); ++i )
	{
		if ( bWindowed )
		{
			const int at_tic = ( i < tics.size( )) ? tics[i] : 0;
			if (( ticMin > 0 ) && ( at_tic < ticMin ))	continue;
			if (( ticMax > 0 ) && ( at_tic > ticMax ))	continue;
		}

		std::map<DWORD64, zx::SampleHit>::iterator at = resolved.find( pcs[i] );
		if ( at == resolved.end( ))
		{
			std::string dso;
			const std::string symbol = SymbolFor( proc, pcs[i], dso );
			at = resolved.insert( std::make_pair( pcs[i], zx::SampleHit( symbol, dso ))).first;
		}

		hits.push_back( at->second );
	}

	SymCleanup( proc );

	// ModuleName has no extension, so the engine is "forkundera" whatever the binary is called.
	if ( engineOnly )
		hits = zx::OnlyFrom( hits, "forkundera" );

	const std::string json = zx::SampleReportJson(
		zx::RankSamples( hits, ( top > 0 ) ? (size_t)top : 0 ), elapsed,
		(unsigned long long)hits.size( ));

	CloseHandle( thread );

	std::lock_guard<std::mutex> guard( g_lock );
	g_report = json;
	g_ready = true;
	g_running = false;
}

#endif // _WIN32

} // namespace

bool MCP_Sample_Arm( double seconds, int hz, bool engineOnly, int top, int ticMin, int ticMax )
{
#ifdef _WIN32
	{
		std::lock_guard<std::mutex> guard( g_lock );
		if ( g_running )
			return false;

		g_running = true;
		g_ready = false;
	}

	// A real handle for the CALLING thread, which is the game thread: GetCurrentThread returns a
	// pseudo-handle that means "whoever is asking", so handing it to another thread would have the
	// sampler suspend ITSELF. The duplicate is closed by the run.
	HANDLE thread = NULL;
	if ( !DuplicateHandle( GetCurrentProcess( ), GetCurrentThread( ),
		GetCurrentProcess( ), &thread,
		THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0 ))
	{
		std::lock_guard<std::mutex> guard( g_lock );
		g_running = false;
		return false;
	}

	std::thread( SampleRun, thread, seconds, hz, engineOnly, top, ticMin, ticMax ).detach( );
	return true;
#else
	// mac and Linux keep the external samplers, which walk the whole stack and see every thread.
	(void)seconds; (void)hz; (void)engineOnly; (void)top; (void)ticMin; (void)ticMax;
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
