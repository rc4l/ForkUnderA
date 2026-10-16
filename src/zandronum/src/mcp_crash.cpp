//
// mcp_crash.cpp -- crash backtrace handler for the MCP bridge (overlay file).
//
// GENERIC: this is part of the engine-bridge overlay, applied by apply-bridge to
// ANY Zandronum source tree (stock or fork). It is not specific to any fork.
//
// On a fatal signal (SIGSEGV/SIGABRT/SIGBUS/SIGILL/SIGFPE on POSIX, or an
// unhandled SEH exception on Windows) it writes the signal, faulting address,
// and a symbolized backtrace to a crash-log file, then lets the process die
// normally (so the socket bridge closes and the MCP notices). The MCP
// `get_crash` tool reads that file back.
//
// Opt-in: like the rest of the bridge, it only arms when ZANDRONUM_BRIDGE_PORT
// is set, so a normal (non-MCP) launch is completely unaffected.
//
// Includes NO engine headers (mirrors mcp_bridge.cpp) so platform system headers
// never clash with the engine's types.
//
#include "mcp_bridge.h"
#include "features/handoff/gamethread.h"   // [rc4l] the game thread the watchdog watches
#include "features/handoff/seam.h"   // [rc4l] the seam each thread was in

// [rc4l] Pure decision helper for chaining to the fatal-signal handler that was installed before
// ours (on a shipped build that is sentry-native's). Declares only an enum + one function in
// namespace zx -- no engine types -- so it does not violate the "no engine headers" rule above
// (it cannot clash with the platform system headers this file relies on).
#include "features/crashreport/computation/signal_chain_compute.h"
#include "features/crashreport/computation/fault_frame_compute.h"

#include <stdlib.h>
#include <cwchar>
#include <stdio.h>
#include <exception>
#include <thread>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <windows.h>
  #include <dbghelp.h>
  #pragma comment(lib, "dbghelp.lib")   // MSVC-only; ignored by GCC/Clang
#else
  #include <signal.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <execinfo.h>
#endif

namespace
{
	char g_crashPath[1024] = { 0 };

	// [rc4l] The crash file's line naming the seam each thread was in, from a static buffer so a crash handler can print it without allocating.
	const char *SeamLine()
	{
		static char line[256];
		const char *mainSeam = zx::handoff::CurrentSeam( false ), *gameSeam = zx::handoff::CurrentSeam( true );
		snprintf( line, sizeof( line ), "seams: main %s, game %s\n", mainSeam ? mainSeam : "none", gameSeam ? gameSeam : "none" );
		return line;
	}
	bool g_installed = false;

	// Resolve where to write the crash log: an explicit ZANDRONUM_CRASH_LOG wins;
	// otherwise derive it from ZANDRONUM_BRIDGE_LOG (<log>.crash); otherwise fall
	// back to a file in the current directory so a crash is never lost.
	void ResolveCrashPath()
	{
		const char *p = getenv( "ZANDRONUM_CRASH_LOG" );
		if ( p && p[0] )
		{
			strncpy( g_crashPath, p, sizeof( g_crashPath ) - 1 );
			return;
		}
		const char *log = getenv( "ZANDRONUM_BRIDGE_LOG" );
		if ( log && log[0] )
		{
			snprintf( g_crashPath, sizeof( g_crashPath ), "%s.crash", log );
			return;
		}
		strncpy( g_crashPath, "zandronum-crash.log", sizeof( g_crashPath ) - 1 );
	}

#ifdef _WIN32
	// [rc4l] The faulting thread's own stack, walked from the exception's context, since the handler's frames above it say nothing about the fault.
	void WriteFaultStack( FILE *f, HANDLE proc, const CONTEXT &faultCtx, SYMBOL_INFO *sym )
	{
#if defined( _M_X64 )
		CONTEXT ctx = faultCtx;
		const zx::FaultFrame start = zx::ComputeFaultFrame( ctx.Rip, ctx.Rsp, ctx.Rip == 0 ? *(const DWORD64 *)ctx.Rsp : 0 );
		ctx.Rip = start.pc;
		ctx.Rsp = start.sp;
		STACKFRAME64 sf;
		memset( &sf, 0, sizeof( sf ) );
		sf.AddrPC.Offset = ctx.Rip;    sf.AddrPC.Mode = AddrModeFlat;
		sf.AddrFrame.Offset = ctx.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
		sf.AddrStack.Offset = ctx.Rsp; sf.AddrStack.Mode = AddrModeFlat;
		fprintf( f, "fault stack:\n" );
		for ( int i = 0; i < 64; ++i )
		{
			if ( !StackWalk64( IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread( ), &sf, &ctx, NULL,
				SymFunctionTableAccess64, SymGetModuleBase64, NULL ) || sf.AddrPC.Offset == 0 )
				break;
			const DWORD64 addr = sf.AddrPC.Offset;
			IMAGEHLP_LINE64 line;
			memset( &line, 0, sizeof( line ) );
			line.SizeOfStruct = sizeof( line );
			DWORD disp = 0;
			const bool hasLine = SymGetLineFromAddr64( proc, addr, &disp, &line ) != FALSE;
			if ( SymFromAddr( proc, addr, 0, sym ) )
				fprintf( f, "  #%d %s + 0x%llx", i, sym->Name, (unsigned long long)( addr - sym->Address ) );
			else
				fprintf( f, "  #%d [0x%llx]", i, (unsigned long long)addr );
			if ( hasLine ) fprintf( f, "  %s:%lu", line.FileName, (unsigned long)line.LineNumber );
			fprintf( f, "\n" );
		}
		fprintf( f, "\n" );
#else
		(void)f; (void)proc; (void)faultCtx; (void)sym;
#endif
	}

	// Unhandled-exception filter: capture and symbolize the stack, write it out,
	// then let the OS continue the normal crash path.
	LONG WINAPI MCP_CrashFilter( EXCEPTION_POINTERS *info )
	{
		FILE *f = fopen( g_crashPath, "w" );
		if ( f )
		{
			fprintf( f, "=== MCP ENGINE CRASH ===\ncode: 0x%08lx\nfault_addr: %p\npid: %lu\n%s\nbacktrace:\n",
				(unsigned long)( info ? info->ExceptionRecord->ExceptionCode : 0 ),
				info ? (void *)info->ExceptionRecord->ExceptionAddress : (void *)0,
				(unsigned long)GetCurrentProcessId(), SeamLine() );

			void *frames[128];
			USHORT n = CaptureStackBackTrace( 0, 128, frames, NULL );

			HANDLE proc = GetCurrentProcess();
			SymSetOptions( SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES );
			SymInitialize( proc, NULL, TRUE );

			char symbuf[sizeof( SYMBOL_INFO ) + 256];
			SYMBOL_INFO *sym = (SYMBOL_INFO *)symbuf;
			sym->SizeOfStruct = sizeof( SYMBOL_INFO );
			sym->MaxNameLen = 255;

			if ( info && info->ContextRecord ) WriteFaultStack( f, proc, *info->ContextRecord, sym );
			fprintf( f, "handler stack:\n" );

			for ( USHORT i = 0; i < n; ++i )
			{
				DWORD64 addr = (DWORD64)(uintptr_t)frames[i];
				if ( SymFromAddr( proc, addr, 0, sym ) )
					fprintf( f, "  #%u %s + 0x%llx  [0x%p]\n", i, sym->Name,
						(unsigned long long)( addr - sym->Address ), frames[i] );
				else
					fprintf( f, "  #%u [0x%p]\n", i, frames[i] );
			}
			fflush( f );
			fclose( f );
		}
		return EXCEPTION_CONTINUE_SEARCH; // let the process crash as usual
	}
#else
	// [rc4l] The fatal signals we hook, and the disposition each had BEFORE we installed ours
	// (captured via sigaction's oldact). We chain to these on a crash so sentry-native -- installed
	// before us -- still captures the report instead of being silently overwritten.
	int g_sigList[] = { SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE };
	struct sigaction g_prevAction[sizeof( g_sigList ) / sizeof( g_sigList[0] )];

	const char *SignalName( int sig )
	{
		switch ( sig )
		{
			case SIGSEGV: return "SIGSEGV (segmentation fault)";
			case SIGABRT: return "SIGABRT (abort)";
			case SIGBUS:  return "SIGBUS (bus error)";
			case SIGILL:  return "SIGILL (illegal instruction)";
			case SIGFPE:  return "SIGFPE (floating point exception)";
			default:      return "signal";
		}
	}

	// Signal handler. Kept close to async-signal-safe: open()/write() are safe,
	// and backtrace_symbols_fd() writes without malloc. backtrace() itself is
	// warmed up at install time so it does no lazy dlopen/allocation here.
	void CrashHandler( int sig, siginfo_t *si, void *ucontext )
	{
		int fd = open( g_crashPath, O_WRONLY | O_CREAT | O_TRUNC, 0644 );
		if ( fd >= 0 )
		{
			char hdr[256];
			int n = snprintf( hdr, sizeof( hdr ),
				"=== MCP ENGINE CRASH ===\nsignal: %d %s\nfault_addr: %p\npid: %d\n%s\nbacktrace:\n",
				sig, SignalName( sig ), si ? si->si_addr : (void *)0, (int)getpid(), SeamLine() );
			if ( n > 0 )
			{
				ssize_t w = write( fd, hdr, (size_t)n );
				(void)w;
			}
			void *frames[128];
			int nf = backtrace( frames, 128 );
			backtrace_symbols_fd( frames, nf, fd );
			close( fd );
		}
		// [rc4l] Chain to whatever fatal-signal handler was installed before ours. On a shipped
		// build that is sentry-native's crash handler, so this is what lets sentry capture the
		// crash even though the MCP bridge layered its own handler on top. Before this, we simply
		// re-raised the default, so sentry never saw the signal -- the bug where a crash under the
		// bridge (e.g. TNT MAP18 with a bot) produced no report. The chaining DECISION is the pure,
		// unit-tested zx::ComputeSignalChainAction; here we just translate the saved sigaction into
		// its inputs and act on the result.
		struct sigaction *prev = NULL;
		for ( unsigned i = 0; i < sizeof( g_sigList ) / sizeof( g_sigList[0] ); ++i )
			if ( g_sigList[i] == sig ) { prev = &g_prevAction[i]; break; }

		if ( prev != NULL )
		{
			const bool usesSiginfo = ( prev->sa_flags & SA_SIGINFO ) != 0;
			void *const saPtr = (void *)prev->sa_sigaction; // aliases sa_handler (a union)
			void *const hPtr  = (void *)prev->sa_handler;
			const bool prevIsSelf = ( saPtr == (void *)CrashHandler );
			const bool sigactionIsReal = saPtr && saPtr != (void *)SIG_DFL && saPtr != (void *)SIG_IGN;
			const bool handlerIsReal   = hPtr  && hPtr  != (void *)SIG_DFL && hPtr  != (void *)SIG_IGN;

			switch ( zx::ComputeSignalChainAction( prevIsSelf, usesSiginfo, sigactionIsReal, handlerIsReal ) )
			{
				case zx::SignalChainAction::CallSigInfo: prev->sa_sigaction( sig, si, ucontext ); break;
				case zx::SignalChainAction::CallPlain:   prev->sa_handler( sig );                 break;
				case zx::SignalChainAction::ReraiseDefault: break;
			}
		}

		// The chained handler returned (or there was none): restore the default and re-raise so the
		// process dies normally (core dump / abort), which closes the bridge socket for the MCP.
		signal( sig, SIG_DFL );
		raise( sig );
	}
#endif
}

#ifdef _WIN32
// [rc4l] abort() and std::terminate DO NOT reach SetUnhandledExceptionFilter.
//
// That filter only sees structured exceptions -- an access violation gets a full symbolised stack in
// <bridge log>.crash, which is how every crash in this project has been read. A std::terminate (an
// exception escaping a thread, a joinable std::thread assigned over or destroyed) and a plain abort()
// go straight to the CRT and take the process down with NO filter call, NO .crash file, NO message
// and NO dump. The log simply stops mid-line.
//
// That is exactly the death that cost most of a session to characterise: four identical failures
// that reported nothing at all, and could not even be distinguished from the bridge watchdog reaping
// the process. Both routes now write the same report the SEH filter does.
void WriteAbortReport( const char *why )
{
	FILE *f = fopen( g_crashPath, "w" );
	if ( f == NULL ) return;
	fprintf( f, "=== MCP ENGINE ABORT ===\nreason: %s\npid: %lu\n%s\nbacktrace:\n",
		why ? why : "(none)", (unsigned long)GetCurrentProcessId(), SeamLine() );
	void *frames[128];
	USHORT n = CaptureStackBackTrace( 0, 128, frames, NULL );
	HANDLE proc = GetCurrentProcess();
	SymSetOptions( SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS );
	SymInitialize( proc, NULL, TRUE );
	char symbuf[sizeof( SYMBOL_INFO ) + 256];
	SYMBOL_INFO *sym = (SYMBOL_INFO *)symbuf;
	sym->SizeOfStruct = sizeof( SYMBOL_INFO );
	sym->MaxNameLen = 255;
	for ( USHORT i = 0; i < n; ++i )
	{
		DWORD64 addr = (DWORD64)(uintptr_t)frames[i];
		if ( SymFromAddr( proc, addr, 0, sym ) )
			fprintf( f, "  #%u %s + 0x%llx  [0x%p]\n", i, sym->Name,
				(unsigned long long)( addr - sym->Address ), frames[i] );
		else
			fprintf( f, "  #%u [0x%p]\n", i, frames[i] );
	}
	fclose( f );
	fprintf( stderr, "fua: ENGINE ABORT (%s) -- report written to %s\n",
		why ? why : "(none)", g_crashPath );
	fflush( stderr );
}

// [rc4l] A VECTORED handler, because the worst deaths never reach the others.
//
// SetUnhandledExceptionFilter runs at the END of exception dispatch and only if nothing handled it.
// The two codes that matter most here skip it: STATUS_HEAP_CORRUPTION (0xC0000374) and
// STATUS_STACK_BUFFER_OVERRUN (0xC0000409, which is also what __fastfail raises) terminate the
// process immediately, with no filter call, no .crash file and no dump. 0xC0000409 is exactly what
// rc4l saw on wdiex02, and the silent death after N map loads has every hallmark of the same family:
// nothing on stderr, no report, the log stopping mid-line.
//
// A vectored handler runs FIRST, ahead of SEH dispatch, so it gets a look in before the process is
// torn down. It reports and then always continues the search -- it never swallows anything.
// [rc4l] The CRT's invalid-parameter path, which is the LAST silent exit left.
//
// Hand the CRT something it considers impossible -- a closed FILE*, a bad iterator, a malformed
// format string -- and it calls the invalid-parameter handler, whose default is to fail fast. That
// is not an SEH exception, not abort(), and not std::terminate, so the unhandled-exception filter,
// the terminate handler and the SIGABRT handler all stay quiet and the process simply evaporates.
// Every other silent route has been closed off and the engine still died saying nothing, so this one
// gets a handler too. In a release CRT the arguments arrive empty, which is why it reports what it
// can and leans on the backtrace for the rest.
// [rc4l] A HANG reports too, because the engine does not always die -- sometimes it spins.
//
// Every handler above catches a process that STOPS. The failure that actually cost this session was
// a process that kept running: after ~21 map changes the main thread went into an infinite loop
// during level setup, the bridge stopped answering, the harness gave up, and only then did the
// watchdog reap it -- so it looked exactly like a silent crash and no handler ever fired.
//
// The main thread bumps a heartbeat every pass of the main loop. If it stops bumping while the
// process is still alive, this suspends it, walks its stack with dbghelp and writes the same report
// a crash would. A spinning thread cannot be asked what it is doing; it has to be taken and read.
volatile LONG   g_heartbeat = 0;
HANDLE          g_mainThread = NULL;
HANDLE          g_gameThread = NULL;   // [rc4l] features/handoff: the thread running tics beside the main one, once there is one
volatile LONG   g_hangReported = 0;

void WriteThreadStack( HANDLE thread, const char *why )
{
	if ( thread == NULL ) return;
	if ( InterlockedExchange( &g_hangReported, 1 ) != 0 ) return;

	FILE *f = fopen( g_crashPath, "w" );
	if ( f == NULL ) return;
	fprintf( f, "=== MCP ENGINE HANG ===\nreason: %s\npid: %lu\n%s\nstuck thread backtrace:\n",
		why, (unsigned long)GetCurrentProcessId(), SeamLine() );

	SuspendThread( thread );
	CONTEXT ctx;
	memset( &ctx, 0, sizeof( ctx ) );
	ctx.ContextFlags = CONTEXT_FULL;
	if ( GetThreadContext( thread, &ctx ) )
	{
		HANDLE proc = GetCurrentProcess();
		SymSetOptions( SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS );
		SymInitialize( proc, NULL, TRUE );

		STACKFRAME64 sf;
		memset( &sf, 0, sizeof( sf ) );
		sf.AddrPC.Offset    = ctx.Rip; sf.AddrPC.Mode    = AddrModeFlat;
		sf.AddrFrame.Offset = ctx.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
		sf.AddrStack.Offset = ctx.Rsp; sf.AddrStack.Mode = AddrModeFlat;

		char symbuf[sizeof( SYMBOL_INFO ) + 256];
		SYMBOL_INFO *sym = (SYMBOL_INFO *)symbuf;
		sym->SizeOfStruct = sizeof( SYMBOL_INFO );
		sym->MaxNameLen = 255;

		for ( int i = 0; i < 64; i++ )
		{
			if ( !StackWalk64( IMAGE_FILE_MACHINE_AMD64, proc, thread, &sf, &ctx, NULL,
					SymFunctionTableAccess64, SymGetModuleBase64, NULL ) )
				break;
			if ( sf.AddrPC.Offset == 0 ) break;
			DWORD64 addr = sf.AddrPC.Offset;
			if ( SymFromAddr( proc, addr, 0, sym ) )
				fprintf( f, "  #%d %s + 0x%llx  [0x%llx]\n", i, sym->Name,
					(unsigned long long)( addr - sym->Address ), (unsigned long long)addr );
			else
				fprintf( f, "  #%d [0x%llx]\n", i, (unsigned long long)addr );
		}
	}
	ResumeThread( thread );
	fclose( f );
	fprintf( stderr, "fua: ENGINE HANG -- stuck-thread stack written to %s\n", g_crashPath );
	fflush( stderr );
}

void HangWatchdog()
{
	LONG last = -1;
	int stalls = 0;
	uint64_t lastGame = 0;
	int gameStalls = 0;
	for ( ;; )
	{
		Sleep( 5000 );
		// [rc4l] features/handoff: a tic in flight on the game thread that stops passing its seams is a hang of its own, which the main thread's heartbeat would not show.
		const uint64_t game = zx::handoff::Heartbeat( true );
		if ( g_gameThread == NULL || !zx::handoff::TicInFlight() || zx::handoff::GameThreadDone() || game != lastGame ) { lastGame = game; gameStalls = 0; }
		else if ( ++gameStalls >= 6 )
		{
			WriteThreadStack( g_gameThread, "game thread stalled for 30s in a tic" );
			return;
		}
		const LONG now = g_heartbeat;
		if ( now != last ) { last = now; stalls = 0; continue; }
		// Level loads legitimately block the main loop, so only a LONG stall counts.
		if ( ++stalls >= 6 )   // 30 seconds without a single pass of the main loop
		{
			WriteThreadStack( g_mainThread, "main loop stalled for 30s" );
			return;
		}
	}
}

void InvalidParameterHandler( const wchar_t *expr, const wchar_t *func, const wchar_t *file,
	unsigned int line, uintptr_t )
{
	char why[256];
	char e[64] = "?", fn[64] = "?";
	if ( expr ) wcstombs( e, expr, sizeof( e ) - 1 );
	if ( func ) wcstombs( fn, func, sizeof( fn ) - 1 );
	(void)file;
	snprintf( why, sizeof( why ), "CRT invalid parameter -- expr '%s' in %s (line %u)", e, fn, line );
	WriteAbortReport( why );
	_exit( 3 );
}

void PureCallHandler()
{
	WriteAbortReport( "pure virtual call" );
	_exit( 3 );
}

LONG WINAPI MCP_VectoredFilter( EXCEPTION_POINTERS *info )
{
	if ( info == NULL || info->ExceptionRecord == NULL ) return EXCEPTION_CONTINUE_SEARCH;
	const DWORD code = info->ExceptionRecord->ExceptionCode;
	// [rc4l] Everything FATAL, not a hand-picked list.
	//
	// This started as five codes and missed 0xC00000FD -- STACK OVERFLOW -- which is precisely the
	// shape the remaining failure has: it dies during level TEARDOWN, silently, and only after enough
	// maps have been loaded for something recursive to get deep enough. A whitelist is the wrong
	// structure for "tell me how it died"; anything in the 0xC0000000 range is a fatal NTSTATUS, and
	// the only regular traffic up there is the C++ throw code, which FFmpeg raises constantly.
	const bool cppThrow = ( code == 0xE06D7363 );
	const bool fatal = ( ( code & 0xF0000000 ) == 0xC0000000 ) && !cppThrow;
	if ( !fatal ) return EXCEPTION_CONTINUE_SEARCH;

	static volatile long reported = 0;
	if ( InterlockedExchange( &reported, 1 ) != 0 ) return EXCEPTION_CONTINUE_SEARCH;

	char why[128];
	snprintf( why, sizeof( why ), "vectored: exception 0x%08lx at %p",
		(unsigned long)code, (void *)info->ExceptionRecord->ExceptionAddress );
	WriteAbortReport( why );
	return EXCEPTION_CONTINUE_SEARCH;
}

void TerminateHandler()
{
	const char *what = "std::terminate";
	// If a live exception caused it, its message is the single most useful fact here.
	static char buf[512];
	try { std::rethrow_exception( std::current_exception() ); }
	catch ( const std::exception &e )
	{
		snprintf( buf, sizeof( buf ), "std::terminate -- uncaught %s", e.what() );
		what = buf;
	}
	catch ( ... ) { what = "std::terminate -- uncaught non-std exception"; }
	WriteAbortReport( what );
	_exit( 3 );
}

void AbortHandler( int )
{
	WriteAbortReport( "abort() / SIGABRT" );
	_exit( 3 );
}
#endif // _WIN32

// [rc4l] features/handoff: the game thread arms its own stack guarantee and hands the watchdog a handle it can read a hang off.
void MCP_Crash_NoteGameThread()
{
#ifdef _WIN32
	ULONG guarantee = 65536;
	SetThreadStackGuarantee( &guarantee );
	DuplicateHandle( GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_gameThread,
		0, FALSE, DUPLICATE_SAME_ACCESS );
#endif
}

// The main thread's proof of life. Cheap enough for the main loop: one interlocked increment.
void MCP_Crash_NoteAlive()
{
#ifdef _WIN32
	InterlockedIncrement( &g_heartbeat );
#endif
}

// Arm the crash handler once. Safe to call repeatedly (cheap no-op after the
// first). No-op unless the bridge is opted in via ZANDRONUM_BRIDGE_PORT.
void MCP_Crash_Init()
{
	if ( g_installed ) return;
	const char *port = getenv( "ZANDRONUM_BRIDGE_PORT" );
	if ( port == NULL || port[0] == '\0' ) return; // opt-in, same gate as the bridge
	g_installed = true;
	ResolveCrashPath();

#ifdef _WIN32
	SetUnhandledExceptionFilter( MCP_CrashFilter );
	// ...and the two routes that filter never sees.
	// [rc4l] Reserve stack for the handler, or a STACK OVERFLOW cannot be reported: the handler runs
	// on the very stack that just ran out. This is what SetThreadStackGuarantee is for.
	{
		ULONG guarantee = 65536;
		SetThreadStackGuarantee( &guarantee );
	}
	AddVectoredExceptionHandler( 1 /* first */, MCP_VectoredFilter );
	_set_invalid_parameter_handler( InvalidParameterHandler );
	_set_purecall_handler( PureCallHandler );
	// A real handle to the thread that armed this -- the main thread -- so a hang can be read off it.
	DuplicateHandle( GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_mainThread,
		0, FALSE, DUPLICATE_SAME_ACCESS );
	std::thread( HangWatchdog ).detach();
	std::set_terminate( TerminateHandler );
	signal( SIGABRT, AbortHandler );
#else
	// Warm up the unwinder so the handler does no lazy work.
	void *warm[4];
	(void)backtrace( warm, 4 );

	struct sigaction sa;
	memset( &sa, 0, sizeof( sa ) );
	sa.sa_sigaction = CrashHandler;
	// SA_ONSTACK: run on the alternate signal stack sentry-native registers, so the chain still
	// works for stack-overflow crashes (and matches sentry's own handler flags). Ignored if no
	// alternate stack is set up.
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigemptyset( &sa.sa_mask );

	// Save the previous disposition (oldact) for each signal so CrashHandler can chain to it.
	for ( unsigned i = 0; i < sizeof( g_sigList ) / sizeof( g_sigList[0] ); ++i )
		sigaction( g_sigList[i], &sa, &g_prevAction[i] );
#endif
}
