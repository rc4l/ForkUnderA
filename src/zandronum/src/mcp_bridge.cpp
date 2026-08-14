//
// mcp_bridge.cpp -- native MCP control-bridge TRANSPORT + LIFECYCLE (engine-free).
//
// This translation unit owns only the loopback socket, the request/response queue, and process
// lifecycle (clean-quit on signal, self-registering pidfile, orphan watchdog). It includes NO engine
// headers -- the platform networking headers would clash with the engine's types -- and forwards the
// few entry points it needs. All the actual "programmable engine" logic (determinism, state, sessions)
// lives in mcp_rpc.cpp, which IS engine-facing. Wire framing is the tested pure core in
// features/mcp-bridge/computation/mcprpc_compute.
//
// Compiled ONLY when FUA_MCP_BRIDGE is defined (dev/test builds). Runtime opt-in: the listener starts
// only when ZANDRONUM_BRIDGE_PORT is set, binds 127.0.0.1 only.
//
#include "mcp_bridge.h"
#include "features/mcp-bridge/computation/mcprpc_compute.h"

// --- Portable socket shim ---------------------------------------------------
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <direct.h>
  typedef SOCKET mcp_socket_t;
  #define MCP_INVALID_SOCKET INVALID_SOCKET
  #define mcp_close_socket   closesocket
  #pragma comment(lib, "ws2_32.lib")   // MSVC-only directive; ignored by GCC/Clang
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <sys/stat.h>
  #include <unistd.h>
  #include <signal.h>
  #include <errno.h>
#endif

#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <deque>
#include <string>
#include <csignal>   // sig_atomic_t (portable; POSIX also gets sigaction via <signal.h> above)
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// --- Engine entry points (implemented in mcp_rpc.cpp / mcp_crash.cpp) --------
// Run one queued RPC request on the game thread and send its response. args is the raw JSON "args".
void MCP_RPC_Dispatch( long id, const char *cmd, const char *args );
// Per-frame housekeeping on the game thread (step re-freeze, event pumping, HUD frame capture).
void MCP_RPC_Tick();

#ifdef _WIN32
  static int  mcp_getpid()          { return (int)GetCurrentProcessId(); }
  static bool mcp_pid_alive( int pid )
  {
      HANDLE h = OpenProcess( SYNCHRONIZE, FALSE, (DWORD)pid );
      if ( h == NULL ) return false;
      DWORD w = WaitForSingleObject( h, 0 );
      CloseHandle( h );
      return w == WAIT_TIMEOUT;
  }
#else
  typedef int mcp_socket_t;
  static int  mcp_getpid()          { return (int)getpid(); }
  static bool mcp_pid_alive( int pid )
  {
      return ::kill( (pid_t)pid, 0 ) == 0 || errno == EPERM;
  }
#endif
#ifndef _WIN32
  #define MCP_INVALID_SOCKET (-1)
  #define mcp_close_socket   ::close
#endif

namespace
{
	bool                     g_initialized = false;
	std::atomic<bool>        g_enabled( false );
	mcp_socket_t             g_listen      = MCP_INVALID_SOCKET;
	mcp_socket_t             g_client      = MCP_INVALID_SOCKET;
	std::mutex               g_qlock;     // guards g_inbound + g_rxbuf
	std::mutex               g_sendlock;  // serialises writes to g_client
	std::deque<zx::mcp::RpcRequest> g_inbound;
	std::string              g_rxbuf;
	std::string              g_pidfilePath;

	// Set by the SIGTERM/SIGINT handler; honoured on the game thread so GL/Cocoa teardown runs
	// on the main thread between frames -- a clean exit(0) instead of a mid-render hard kill that
	// wedges the macOS window server.
	volatile sig_atomic_t    g_quitRequested = 0;

	int BridgePort()
	{
		const char *env = getenv( "ZANDRONUM_BRIDGE_PORT" );
		if ( env == NULL || env[0] == '\0' ) return 0; // opt-in
		int port = atoi( env );
		return ( port > 0 && port < 65536 ) ? port : 0;
	}

	int ParentPid()
	{
		const char *env = getenv( "ZANDRONUM_BRIDGE_PARENT_PID" );
		if ( env == NULL || env[0] == '\0' ) return 0;
		int pid = atoi( env );
		return pid > 0 ? pid : 0;
	}

	// Optional shared secret. When ZANDRONUM_BRIDGE_TOKEN is set, a client must present the same
	// token in its "hello" or first request before any command runs -- so no OTHER local process can
	// drive an armed bridge. Empty => no token required (still loopback + opt-in).
	const char *BridgeToken()
	{
		const char *env = getenv( "ZANDRONUM_BRIDGE_TOKEN" );
		return ( env && env[0] ) ? env : NULL;
	}
	bool g_authed = false; // per-connection: has the client presented the token (if one is required)?

	void RemovePidfile()
	{
		if ( !g_pidfilePath.empty() ) { remove( g_pidfilePath.c_str() ); g_pidfilePath.clear(); }
	}

	// ~/.forkundera/instances/<pid>.json -- so a reaper (fuactl reap) can find EVERY instance,
	// however it was launched (MCP, fuactl, `open`, or by hand). Removed on clean exit via atexit.
	void WritePidfile( int port )
	{
#ifdef _WIN32
		const char *home = getenv( "USERPROFILE" );
#else
		const char *home = getenv( "HOME" );
#endif
		if ( home == NULL || home[0] == '\0' ) return;
		std::string dir = std::string( home ) + "/.forkundera";
#ifdef _WIN32
		_mkdir( dir.c_str() );
		dir += "/instances"; _mkdir( dir.c_str() );
#else
		mkdir( dir.c_str(), 0755 );
		dir += "/instances"; mkdir( dir.c_str(), 0755 );
#endif
		char path[1024];
		snprintf( path, sizeof( path ), "%s/%d.json", dir.c_str(), mcp_getpid() );
		FILE *f = fopen( path, "w" );
		if ( f == NULL ) return;
		fprintf( f, "{\"pid\":%d,\"port\":%d,\"ppid\":%d,\"bridge\":\"2.0.0\"}\n",
			mcp_getpid(), port, ParentPid() );
		fflush( f );
		fclose( f );
		g_pidfilePath = path;
		atexit( RemovePidfile ); // clean exit removes it; a hard-killed engine's stale entry is pruned
		                         // by the reaper (fuactl reap) via a liveness check, so the registry
		                         // is self-healing either way.
	}

#ifndef _WIN32
	void OnTermSignal( int ) { g_quitRequested = 1; }
	void InstallSignalHandlers()
	{
		// Only SIGTERM/SIGINT. Fault signals (SEGV/BUS/ILL/FPE) belong to sentry-native's crash
		// handler installed earlier in startup -- do NOT touch those.
		struct sigaction sa;
		memset( &sa, 0, sizeof( sa ) );
		sa.sa_handler = OnTermSignal;
		sigaction( SIGTERM, &sa, NULL );
		sigaction( SIGINT, &sa, NULL );
	}
#else
	static BOOL WINAPI CtrlHandler( DWORD ) { g_quitRequested = 1; return TRUE; }
	void InstallSignalHandlers() { SetConsoleCtrlHandler( CtrlHandler, TRUE ); }
#endif
}

// Send one framed JSON line to the connected client. Thread-safe; called from the game thread
// (responses/events) and the listen thread (hello). Public so mcp_rpc.cpp can emit results/events.
void MCP_Bridge_SendJson( const char *json )
{
	std::lock_guard<std::mutex> lk( g_sendlock );
	mcp_socket_t s = g_client;
	if ( s == MCP_INVALID_SOCKET ) return;
	std::string wire = json;
	wire.push_back( '\n' );
	if ( send( s, wire.c_str(), (int)wire.size(), 0 ) < 0 )
	{
		mcp_close_socket( s );
		g_client = MCP_INVALID_SOCKET;
	}
}

namespace
{
	// Startup logfile tee (captures DECORATE/ACS compile errors that abort before a client connects).
	void LogWrite( const char *text )
	{
		static bool  checked = false;
		static FILE *logf    = NULL;
		if ( !checked )
		{
			checked = true;
			const char *path = getenv( "ZANDRONUM_BRIDGE_LOG" );
			if ( path && path[0] ) logf = fopen( path, "w" );
		}
		if ( logf ) { fputs( text, logf ); fflush( logf ); }
	}

	void WatchdogThread( int parentPid )
	{
		int misses = 0;
		for ( ;; )
		{
			std::this_thread::sleep_for( std::chrono::milliseconds( 1500 ) );
			if ( mcp_pid_alive( parentPid ) ) { misses = 0; continue; }
			if ( ++misses >= 2 )
			{
				// Ask for a CLEAN quit first (game thread runs GL/Cocoa teardown), then hard-exit as a
				// last resort if the loop is wedged and doesn't honour the flag within a grace window.
				g_quitRequested = 1;
				std::this_thread::sleep_for( std::chrono::milliseconds( 3000 ) );
#ifdef _WIN32
				TerminateProcess( GetCurrentProcess(), 0 );
#else
				_exit( 0 );
#endif
			}
		}
	}

	void ListenThread()
	{
		for ( ;; )
		{
			mcp_socket_t s = accept( g_listen, NULL, NULL );
			if ( s == MCP_INVALID_SOCKET )
			{
				std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
				continue;
			}

			{
				std::lock_guard<std::mutex> lk( g_sendlock );
				if ( g_client != MCP_INVALID_SOCKET ) mcp_close_socket( g_client );
				g_client = s;
			}
			{
				std::lock_guard<std::mutex> lk( g_qlock );
				g_rxbuf.clear();
			}
			g_authed = ( BridgeToken() == NULL ); // no token required => already authed

			char hello[320];
			snprintf( hello, sizeof( hello ),
				"{\"t\":\"hello\",\"engine\":\"forkundera\",\"bridge\":\"2.0.0\",\"pid\":%d,"
				"\"auth\":%s,\"caps\":[\"rpc\",\"events\",\"determinism\",\"snapshot\",\"session\"]}",
				mcp_getpid(), g_authed ? "false" : "true" ); // auth:false means "token still required"
			MCP_Bridge_SendJson( hello );

			char buf[2048];
			for ( ;; )
			{
				int n = (int)recv( s, buf, sizeof( buf ), 0 );
				if ( n <= 0 ) break;
				std::lock_guard<std::mutex> lk( g_qlock );
				g_rxbuf.append( buf, n );
				size_t nl;
				while ( ( nl = g_rxbuf.find( '\n' ) ) != std::string::npos )
				{
					std::string line = g_rxbuf.substr( 0, nl );
					g_rxbuf.erase( 0, nl + 1 );

					// Token gate: the first message must be {"cmd":"auth","args":{"token":"..."}}.
					if ( !g_authed )
					{
						std::string tok;
						if ( zx::mcp::GetStr( line, "token", tok ) )
						{
							const char *want = BridgeToken();
							if ( want && tok == want ) { g_authed = true; MCP_Bridge_SendJson( "{\"t\":\"authed\"}" ); }
							else MCP_Bridge_SendJson( "{\"t\":\"error\",\"error\":\"bad token\"}" );
						}
						continue; // drop everything until authed
					}

					zx::mcp::RpcRequest req = zx::mcp::ParseRequest( line );
					if ( req.valid )
						g_inbound.push_back( req );
				}
			}

			{
				std::lock_guard<std::mutex> lk( g_sendlock );
				if ( g_client == s ) g_client = MCP_INVALID_SOCKET;
			}
			mcp_close_socket( s );
		}
	}

	void Init()
	{
		g_initialized = true;
		int port = BridgePort();
		if ( port == 0 ) return; // opt-in

		InstallSignalHandlers();
		WritePidfile( port );

		int ppid = ParentPid();
		if ( ppid != 0 )
			std::thread( WatchdogThread, ppid ).detach();

#ifdef _WIN32
		WSADATA wsa;
		if ( WSAStartup( MAKEWORD( 2, 2 ), &wsa ) != 0 ) return;
#endif
		g_listen = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
		if ( g_listen == MCP_INVALID_SOCKET ) return;

		int yes = 1;
		setsockopt( g_listen, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof( yes ) );

		sockaddr_in addr;
		memset( &addr, 0, sizeof( addr ) );
		addr.sin_family = AF_INET;
		addr.sin_port = htons( (unsigned short)port );
		inet_pton( AF_INET, "127.0.0.1", &addr.sin_addr ); // loopback only

		if ( bind( g_listen, (sockaddr *)&addr, sizeof( addr ) ) < 0 ||
			 listen( g_listen, 1 ) < 0 )
		{
			mcp_close_socket( g_listen );
			g_listen = MCP_INVALID_SOCKET;
			return;
		}

		std::thread( ListenThread ).detach();
		g_enabled = true;
	}
}

void MCP_Bridge_Poll()
{
	MCP_Crash_Init();
	if ( !g_initialized ) Init();
	if ( !g_enabled ) return;

	// Honour a pending clean-quit request from a signal or the watchdog, between frames, on the main
	// thread -- so exit(0) runs the full atexit/call_terms teardown (GL, SDL, Cocoa) with no wedge.
	if ( g_quitRequested )
	{
		RemovePidfile();
		exit( 0 );
	}

	MCP_RPC_Tick(); // step re-freeze, event pump, HUD frame capture (engine-facing)

	for ( ;; )
	{
		zx::mcp::RpcRequest req;
		bool have = false;
		{
			std::lock_guard<std::mutex> lk( g_qlock );
			if ( !g_inbound.empty() ) { req = g_inbound.front(); g_inbound.pop_front(); have = true; }
		}
		if ( !have ) break;
		MCP_RPC_Dispatch( req.id, req.cmd.c_str(), req.args.c_str() );
	}
}

void MCP_Bridge_TeeOutput( const char *text )
{
	if ( text == NULL ) return;
	MCP_Crash_Init();
	LogWrite( text );
	if ( !g_enabled || g_client == MCP_INVALID_SOCKET ) return;
	std::string esc;
	zx::mcp::JsonEscape( text, esc );
	std::string data = "{\"text\":\"" + esc + "\"}";
	MCP_Bridge_SendJson( zx::mcp::BuildEvent( "out", data ).c_str() );
}

void MCP_Bridge_Shutdown()
{
	std::lock_guard<std::mutex> lk( g_sendlock );
	if ( g_client != MCP_INVALID_SOCKET ) { mcp_close_socket( g_client ); g_client = MCP_INVALID_SOCKET; }
	if ( g_listen != MCP_INVALID_SOCKET ) { mcp_close_socket( g_listen ); g_listen = MCP_INVALID_SOCKET; }
	RemovePidfile();
}

bool MCP_InputLocked()
{
	// Opt-in per instance and read once. A harness-driven instance sets ZANDRONUM_BRIDGE_INPUT_LOCK so
	// its on-screen window is hands-off: the OS input pump drops keyboard/mouse and only bridge-injected
	// events (input.event / input.look / input.axis, which bypass the OS layer) move the sim. Leaving it
	// unset keeps a bridge instance manually controllable, e.g. for debugging.
	static const bool locked = []{
		const char *env = getenv( "ZANDRONUM_BRIDGE_INPUT_LOCK" );
		return env != NULL && env[0] != '\0' && env[0] != '0';
	}();
	return locked;
}
