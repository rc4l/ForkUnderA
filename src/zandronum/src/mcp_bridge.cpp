//
// mcp_bridge.cpp -- native MCP control-bridge TRANSPORT + LIFECYCLE (engine-free).
//
// This translation unit owns only the loopback socket, the request/response queue, and process
// lifecycle (clean-quit on signal, self-registering pidfile, orphan watchdog). It includes NO engine
// headers -- the platform networking headers would clash with the engine's types -- and forwards the
// few entry points it needs. All the actual "programmable engine" logic (determinism, state, sessions)
// lives in mcp_rpc.cpp, which IS engine-facing. Wire framing is the tested pure core in
// features/mcp-bridge/computation/mcprpc_compute, and each connection's reading in bridgeclient_compute.
//
// Compiled ONLY when FUA_MCP_BRIDGE is defined (dev/test builds). Runtime opt-in: the listener starts
// only when ZANDRONUM_BRIDGE_PORT is set, binds 127.0.0.1 only.
//
#include "mcp_bridge.h"
#include "features/mcp-bridge/computation/mcprpc_compute.h"
#include "features/mcp-bridge/computation/bridgeclient_compute.h"

// --- Portable socket shim ---------------------------------------------------
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <direct.h>
  typedef SOCKET mcp_socket_t;
  #define MCP_INVALID_SOCKET INVALID_SOCKET
  #define mcp_close_socket   closesocket
  #define MCP_SHUT_BOTH      SD_BOTH
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
#include <memory>
#include <vector>
#include <algorithm>
#include <string>
#include <csignal>   // sig_atomic_t (portable; POSIX also gets sigaction via <signal.h> above)
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "features/json/computation/json_compute.h"

// --- Engine entry points (implemented in mcp_rpc.cpp / mcp_crash.cpp) --------
// Run one queued RPC request on the main thread and send its response. args is the raw JSON "args".
void MCP_RPC_Dispatch( long id, const char *cmd, const char *args );
// Per-frame housekeeping on the main thread (step re-freeze, event pumping, HUD frame capture).
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
  #define MCP_SHUT_BOTH      SHUT_RDWR
#endif
// [rc4l] A write to a client that hung up raises SIGPIPE on Linux and macOS, which kills the engine, so each send and socket opts out.
#ifdef MSG_NOSIGNAL
  #define MCP_SEND_FLAGS MSG_NOSIGNAL
#else
  #define MCP_SEND_FLAGS 0
#endif

namespace
{
	bool                     g_initialized = false;
	std::atomic<bool>        g_enabled( false );
	mcp_socket_t             g_listen      = MCP_INVALID_SOCKET;
	// [rc4l] One connection, read by its own thread and written to under g_sendlock.
	struct Client
	{
		mcp_socket_t sock   = MCP_INVALID_SOCKET;
		bool         open   = true;
		bool         authed = false;
	};
	// [rc4l] A request and the connection its reply goes back to.
	struct Inbound
	{
		std::shared_ptr<Client> from;
		zx::mcp::RpcRequest     req;
	};
	// [rc4l] The state the detached client threads share is never destroyed, since one can still be running when exit() destroys statics.
	std::mutex               &g_qlock = *new std::mutex();     // [rc4l] guards g_inbound
	std::mutex               &g_sendlock = *new std::mutex();  // [rc4l] guards g_clients, each Client's flags and every write to one
	std::vector<std::shared_ptr<Client>> &g_clients = *new std::vector<std::shared_ptr<Client>>();
	std::atomic<int>         g_listeners( 0 );  // [rc4l] authed open connections, so a print nobody hears builds no event
	std::deque<Inbound>      &g_inbound = *new std::deque<Inbound>();
	std::shared_ptr<Client>  g_replyTo;   // [rc4l] main thread only: whose request is being dispatched
	std::string              g_pidfilePath;

	// Set by the SIGTERM/SIGINT handler; honoured on the main thread so GL/Cocoa teardown runs
	// on the main thread between frames -- a clean exit(0) instead of a mid-render hard kill that
	// wedges the macOS window server.
	// [Doruk-Aksoy] Atomic, not volatile sig_atomic_t, since the signal lands on whichever thread the kernel picks (a Diligent pool worker under ThreadSanitizer) and the watchdog thread sets it too.
	std::atomic<int>         g_quitRequested( 0 );
	static_assert( ATOMIC_INT_LOCK_FREE == 2, "a signal handler may only touch a lock-free atomic" );

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
	void OnTermSignal( int ) { g_quitRequested.store( 1, std::memory_order_relaxed ); }
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
	static BOOL WINAPI CtrlHandler( DWORD ) { g_quitRequested.store( 1, std::memory_order_relaxed ); return TRUE; }
	void InstallSignalHandlers() { SetConsoleCtrlHandler( CtrlHandler, TRUE ); }
#endif
}

namespace
{
	// [rc4l] Marks a connection gone and wakes its reader, which closes the socket; the caller holds g_sendlock.
	void CloseLocked( Client &c )
	{
		if ( !c.open ) return;
		c.open = false;
		if ( c.authed ) --g_listeners;
		shutdown( c.sock, MCP_SHUT_BOTH );
	}

	// [rc4l] Counts a connection as listening once it has shown the token; the caller holds g_sendlock.
	void SetAuthedLocked( Client &c, bool authed )
	{
		if ( !c.open || !authed || c.authed ) return;
		c.authed = true;
		++g_listeners;
	}

	// [rc4l] Writes one framed line to one connection, closing it on a failed write; the caller holds g_sendlock.
	void SendLocked( Client &c, const std::string &wire )
	{
		for ( size_t off = 0; c.open && off < wire.size(); )
		{
			int n = (int)send( c.sock, wire.c_str() + off, (int)( wire.size() - off ), MCP_SEND_FLAGS );
			if ( n <= 0 ) CloseLocked( c );
			else off += (size_t)n;
		}
	}

	std::string Framed( const char *json )
	{
		std::string wire = json;
		wire.push_back( '\n' );
		return wire;
	}
}

// [rc4l] Sends one framed JSON line to every authed connection, from any thread; replies go through MCP_Bridge_Reply.
void MCP_Bridge_SendJson( const char *json )
{
	const std::string wire = Framed( json );
	std::lock_guard<std::mutex> lk( g_sendlock );
	for ( const std::shared_ptr<Client> &c : g_clients )
		if ( c->authed ) SendLocked( *c, wire );
}

// [rc4l] Sends one framed JSON line to the connection whose request the main thread is dispatching.
void MCP_Bridge_Reply( const char *json )
{
	if ( g_replyTo == nullptr ) return;
	std::lock_guard<std::mutex> lk( g_sendlock );
	SendLocked( *g_replyTo, Framed( json ) );
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
				// [rc4l] SAY SO on the way out. TerminateProcess below is the quietest death there
				// is -- exit code 0, no message, no crash dump, and the log stops mid-line -- and an
				// engine reaped here is indistinguishable from one that crashed. A whole debugging
				// session went into telling those two apart; this line settles it in one word.
				fprintf( stderr, "fua: bridge watchdog -- supervising process %d is gone, quitting\n",
					parentPid );
				fflush( stderr );
				// Ask for a CLEAN quit first (main thread runs GL/Cocoa teardown), then hard-exit as a
				// last resort if the loop is wedged and doesn't honour the flag within a grace window.
				g_quitRequested.store( 1, std::memory_order_relaxed );
				std::this_thread::sleep_for( std::chrono::milliseconds( 3000 ) );
#ifdef _WIN32
				TerminateProcess( GetCurrentProcess(), 0 );
#else
				_exit( 0 );
#endif
			}
		}
	}

	// [rc4l] Reads one connection until it closes, queueing its requests for the main thread with itself as the reply's address.
	void ClientThread( std::shared_ptr<Client> c )
	{
		zx::mcp::ClientFeed feed = zx::mcp::ComputeClientOpen( BridgeToken() );
		{
			std::lock_guard<std::mutex> lk( g_sendlock );
			SendLocked( *c, Framed( zx::mcp::ComputeClientHello( mcp_getpid(), !feed.authed ).c_str() ) );
			SetAuthedLocked( *c, feed.authed );
		}

		char buf[2048];
		for ( ;; )
		{
			int n = (int)recv( c->sock, buf, sizeof( buf ), 0 );
			if ( n <= 0 ) break;
			zx::mcp::FeedResult got = zx::mcp::ComputeClientFeed( feed, buf, (size_t)n, BridgeToken() );
			{
				std::lock_guard<std::mutex> lk( g_sendlock );
				SetAuthedLocked( *c, feed.authed );
				for ( const std::string &reply : got.replies ) SendLocked( *c, Framed( reply.c_str() ) );
			}
			std::lock_guard<std::mutex> lk( g_qlock );
			for ( const zx::mcp::RpcRequest &req : got.requests ) g_inbound.push_back( Inbound{ c, req } );
		}

		{
			std::lock_guard<std::mutex> lk( g_sendlock );
			CloseLocked( *c );
			auto it = std::find( g_clients.begin(), g_clients.end(), c );
			if ( it != g_clients.end() ) g_clients.erase( it );
		}
		mcp_close_socket( c->sock );
	}

	// [rc4l] Gives every connection its own reader, so a second fuactl beside a running one is served instead of waiting in the backlog.
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
#ifdef SO_NOSIGPIPE
			int one = 1;
			setsockopt( s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof( one ) );
#endif
			std::shared_ptr<Client> c = std::make_shared<Client>();
			c->sock = s;
			{
				std::lock_guard<std::mutex> lk( g_sendlock );
				g_clients.push_back( c );
			}
			std::thread( ClientThread, c ).detach();
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
			 listen( g_listen, 8 ) < 0 )
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
	if ( g_quitRequested.load( std::memory_order_relaxed ) )
	{
		RemovePidfile();
		exit( 0 );
	}

	MCP_Crash_NoteAlive(); // proof of life for the hang watchdog
	MCP_RPC_Tick(); // step re-freeze, event pump, HUD frame capture (engine-facing)

	for ( ;; )
	{
		Inbound in;
		{
			std::lock_guard<std::mutex> lk( g_qlock );
			if ( g_inbound.empty() ) break;
			in = g_inbound.front();
			g_inbound.pop_front();
		}
		g_replyTo = in.from;
		MCP_RPC_Dispatch( in.req.id, in.req.cmd.c_str(), in.req.args.c_str() );
		g_replyTo.reset();
	}
}

void MCP_Bridge_TeeOutput( const char *text )
{
	if ( text == NULL ) return;
	MCP_Crash_Init();
	LogWrite( text );
	if ( !g_enabled || g_listeners == 0 ) return;
	std::string data = "{\"text\":" + zx::json::ComputeJsonQuoted( text ) + "}";
	MCP_Bridge_SendJson( zx::mcp::BuildEvent( "out", data ).c_str() );
}

void MCP_Bridge_Shutdown()
{
	std::lock_guard<std::mutex> lk( g_sendlock );
	for ( const std::shared_ptr<Client> &c : g_clients ) CloseLocked( *c );
	if ( g_listen != MCP_INVALID_SOCKET ) { mcp_close_socket( g_listen ); g_listen = MCP_INVALID_SOCKET; }
	RemovePidfile();
}

// [Doruk-Aksoy] Whether environment flag `name` is on: set, and neither empty nor 0.
static bool EnvFlagOn( const char *name )
{
	const char *env = getenv( name );
	return env != NULL && env[0] != '\0' && env[0] != '0';
}

bool MCP_InputLocked()
{
	// Opt-in per instance and read once. A harness-driven instance sets ZANDRONUM_BRIDGE_INPUT_LOCK so
	// its on-screen window is hands-off: the OS input pump drops keyboard/mouse and only bridge-injected
	// events (input.event / input.look / input.axis, which bypass the OS layer) move the sim. Leaving it
	// unset keeps a bridge instance manually controllable, e.g. for debugging.
	static const bool locked = EnvFlagOn( "ZANDRONUM_BRIDGE_INPUT_LOCK" );
	return locked;
}

bool MCP_NoFocus()
{
	static const bool noFocus = MCP_InputLocked( ) || EnvFlagOn( "ZANDRONUM_BRIDGE_NO_FOCUS" );
	return noFocus;
}
