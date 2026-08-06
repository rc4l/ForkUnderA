// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] networkheaders.h rather than a raw <windows.h>. The build reshapes the modern Windows
// SDK into the legacy DirectX layout under dxsdk/, and that directory is on the include path -- so
// an unqualified windows.h finds the reshaped winnt.h and fails with two hundred C2733s naming
// intrinsics. This header is how every other engine file gets there, and it works.
//
// It must come FIRST, before any engine header: doomtype.h and friends pull in parts of the SDK
// themselves, and once they have, the reshaped copy is already the one in play.
#include "networkheaders.h"

#include "features/server-hosting/zx_hostprocess.h"
#include "features/server-hosting/computation/hostargs_compute.h"

#include "doomtype.h"
#include "c_dispatch.h"
#include "templates.h"

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#endif

namespace zx
{

namespace
{

#ifdef _WIN32

HANDLE	g_hJob			= NULL;		// the thing that actually guarantees the kill
HANDLE	g_hProcess		= NULL;
HANDLE	g_hReadPipe		= NULL;
DWORD	g_ExitCode		= 0;
bool	g_bStarted		= false;
bool	g_bExited		= false;

// [rc4l] The job object is the whole promise. KILL_ON_JOB_CLOSE means the kernel destroys every
// process in the job when the last handle to it closes -- and handles close when a process dies, no
// matter how. A task-manager kill of the game therefore takes the server with it, which nothing we
// could write in our own shutdown path can promise.
bool CreateKillOnCloseJob( void )
{
	g_hJob = CreateJobObject( NULL, NULL );
	if ( g_hJob == NULL )
		return false;

	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
	memset( &limits, 0, sizeof( limits ));
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

	if ( SetInformationJobObject( g_hJob, JobObjectExtendedLimitInformation,
		&limits, sizeof( limits )) == FALSE )
	{
		CloseHandle( g_hJob );
		g_hJob = NULL;
		return false;
	}

	return true;
}

void CloseAll( void )
{
	if ( g_hReadPipe != NULL )
	{
		CloseHandle( g_hReadPipe );
		g_hReadPipe = NULL;
	}
	if ( g_hProcess != NULL )
	{
		CloseHandle( g_hProcess );
		g_hProcess = NULL;
	}
	// Closing the job is what kills anything still inside it.
	if ( g_hJob != NULL )
	{
		CloseHandle( g_hJob );
		g_hJob = NULL;
	}
}

#else

pid_t	g_Child			= -1;
int		g_ReadFd		= -1;
int		g_ExitCode		= 0;
bool	g_bStarted		= false;
bool	g_bExited		= false;

void CloseAll( void )
{
	if ( g_ReadFd >= 0 )
	{
		close( g_ReadFd );
		g_ReadFd = -1;
	}
	g_Child = -1;
}

#endif

} // namespace

//*****************************************************************************
//
bool HostProcessStart( const std::vector<std::string> &args, std::string &outError )
{
	if ( args.empty( ))
	{
		outError = "No server executable to start.";
		return false;
	}

	// One at a time. A second host while the first is up would leak the first one's handle, and with
	// it the only thing holding the guarantee that it dies with us.
	if ( HostProcessRunning( ))
		HostProcessStop( );

#ifdef _WIN32

	if ( CreateKillOnCloseJob( ) == false )
	{
		outError = "Could not create the job object that keeps the server tied to this game.";
		return false;
	}

	SECURITY_ATTRIBUTES security;
	memset( &security, 0, sizeof( security ));
	security.nLength = sizeof( security );
	security.bInheritHandle = TRUE;

	HANDLE hWrite = NULL;
	if ( CreatePipe( &g_hReadPipe, &hWrite, &security, 0 ) == FALSE )
	{
		CloseAll( );
		outError = "Could not create a pipe to read the server's output.";
		return false;
	}

	// Only the write end is inherited. Leaving our read end inheritable means the child holds a copy
	// too, and the pipe then never reports end-of-file when the child dies -- so a crashed server
	// would look like a silent one forever.
	SetHandleInformation( g_hReadPipe, HANDLE_FLAG_INHERIT, 0 );

	STARTUPINFOA startup;
	memset( &startup, 0, sizeof( startup ));
	startup.cb = sizeof( startup );
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdOutput = hWrite;
	startup.hStdError = hWrite;
	startup.hStdInput = GetStdHandle( STD_INPUT_HANDLE );

	std::string commandLine = JoinWindowsCommandLine( args );
	std::vector<char> mutableLine( commandLine.begin( ), commandLine.end( ));
	mutableLine.push_back( '\0' );

	PROCESS_INFORMATION info;
	memset( &info, 0, sizeof( info ));

	// CREATE_SUSPENDED so it joins the job BEFORE it can run -- and therefore before it can spawn
	// anything of its own that would escape the job it was never in.
	const BOOL bCreated = CreateProcessA( args[0].c_str( ), &mutableLine[0], NULL, NULL, TRUE,
		CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL, &startup, &info );

	CloseHandle( hWrite );

	if ( bCreated == FALSE )
	{
		const DWORD error = GetLastError( );
		CloseAll( );

		char buffer[128];
		mysnprintf( buffer, countof( buffer ),
			"The server could not be started (Windows error %lu).", static_cast<unsigned long>( error ));
		outError = buffer;
		return false;
	}

	if ( AssignProcessToJobObject( g_hJob, info.hProcess ) == FALSE )
	{
		// Without the job we cannot promise the child dies with us, and a server we cannot promise
		// to clean up is worse than no server. Kill it and refuse.
		TerminateProcess( info.hProcess, 1 );
		CloseHandle( info.hThread );
		CloseHandle( info.hProcess );
		CloseAll( );
		outError = "Could not tie the server's lifetime to this game.";
		return false;
	}

	ResumeThread( info.hThread );
	CloseHandle( info.hThread );

	g_hProcess = info.hProcess;
	g_ExitCode = 0;
	g_bStarted = true;
	g_bExited = false;
	return true;

#else

	int pipeFds[2];
	if ( pipe( pipeFds ) != 0 )
	{
		outError = "Could not create a pipe to read the server's output.";
		return false;
	}

	// argv for execv: NULL-terminated, pointing into strings we keep alive across the fork.
	std::vector<char *> argv;
	for ( size_t i = 0; i < args.size( ); ++i )
		argv.push_back( const_cast<char *>( args[i].c_str( )));
	argv.push_back( NULL );

	const pid_t child = fork( );
	if ( child < 0 )
	{
		close( pipeFds[0] );
		close( pipeFds[1] );
		outError = "The server could not be started.";
		return false;
	}

	if ( child == 0 )
	{
		// --- child ---
		close( pipeFds[0] );
		dup2( pipeFds[1], STDOUT_FILENO );
		dup2( pipeFds[1], STDERR_FILENO );
		close( pipeFds[1] );

#ifdef __linux__
		// [rc4l] Die with the parent, whatever happens to it. Set before the exec so it is already
		// in force by the time the server has any code of its own running.
		prctl( PR_SET_PDEATHSIG, SIGKILL );

		// The parent may already have died in the window between fork and here, in which case the
		// signal we just armed will never come.
		if ( getppid( ) == 1 )
			_exit( 1 );
#endif

		// Our own process group, so a signal sent to the game's group does not race us to the kill.
		setpgid( 0, 0 );

		execv( argv[0], &argv[0] );
		_exit( 127 );		// only reached if exec failed
	}

	// --- parent ---
	close( pipeFds[1] );

	// Non-blocking, because HostProcessReadOutput is called from the game loop and must never wait
	// on a server that has nothing to say.
	const int flags = fcntl( pipeFds[0], F_GETFL, 0 );
	fcntl( pipeFds[0], F_SETFL, flags | O_NONBLOCK );

	g_ReadFd = pipeFds[0];
	g_Child = child;
	g_ExitCode = 0;
	g_bStarted = true;
	g_bExited = false;
	return true;

#endif
}

//*****************************************************************************
//
bool HostProcessRunning( void )
{
	if ( g_bStarted == false )
		return false;

#ifdef _WIN32

	if ( g_hProcess == NULL )
		return false;

	DWORD code = 0;
	if ( GetExitCodeProcess( g_hProcess, &code ) && ( code != STILL_ACTIVE ))
	{
		g_ExitCode = static_cast<DWORD>( code );
		g_bExited = true;
		return false;
	}

	return !g_bExited;

#else

	if ( g_Child <= 0 )
		return false;

	int status = 0;
	const pid_t done = waitpid( g_Child, &status, WNOHANG );
	if ( done == g_Child )
	{
		g_ExitCode = WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
		g_bExited = true;
		return false;
	}

	return !g_bExited;

#endif
}

//*****************************************************************************
//
std::string HostProcessReadOutput( void )
{
	std::string out;

#ifdef _WIN32

	if ( g_hReadPipe == NULL )
		return out;

	for ( ;; )
	{
		DWORD available = 0;
		// PeekNamedPipe first: ReadFile on a pipe with nothing in it blocks, and this runs on the
		// game loop.
		if ( PeekNamedPipe( g_hReadPipe, NULL, 0, NULL, &available, NULL ) == FALSE )
			break;
		if ( available == 0 )
			break;

		char buffer[2048];
		DWORD read = 0;
		// Not MIN( ): the engine's template overloads pick up zx::Fixed and the call is ambiguous.
		const DWORD cap = static_cast<DWORD>( sizeof( buffer ));
		const DWORD want = ( available < cap ) ? available : cap;
		if ( ReadFile( g_hReadPipe, buffer, want, &read, NULL ) == FALSE )
			break;
		if ( read == 0 )
			break;

		out.append( buffer, read );
	}

#else

	if ( g_ReadFd < 0 )
		return out;

	for ( ;; )
	{
		char buffer[2048];
		const ssize_t read = ::read( g_ReadFd, buffer, sizeof( buffer ));
		if ( read > 0 )
		{
			out.append( buffer, static_cast<size_t>( read ));
			continue;
		}

		// EINTR is a signal, not an end. Anything else means nothing more is waiting right now.
		if (( read < 0 ) && ( errno == EINTR ))
			continue;
		break;
	}

#endif

	return out;
}

//*****************************************************************************
//
void HostProcessStop( void )
{
	if ( g_bStarted == false )
		return;

#ifdef _WIN32

	if (( g_hProcess != NULL ) && ( g_bExited == false ))
	{
		// No polite shutdown to attempt: the child is a windowless GUI process with no console to
		// send a break to. Terminate is what we have, and the server writes nothing we would lose.
		TerminateProcess( g_hProcess, 0 );
		WaitForSingleObject( g_hProcess, 3000 );

		DWORD code = 0;
		if ( GetExitCodeProcess( g_hProcess, &code ))
			g_ExitCode = code;
	}

#else

	if (( g_Child > 0 ) && ( g_bExited == false ))
	{
		// SIGTERM first so it can shut down cleanly, SIGKILL if it will not.
		kill( g_Child, SIGTERM );

		bool bGone = false;
		for ( int i = 0; i < 30; ++i )
		{
			int status = 0;
			if ( waitpid( g_Child, &status, WNOHANG ) == g_Child )
			{
				g_ExitCode = WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
				bGone = true;
				break;
			}
			usleep( 100000 );
		}

		if ( bGone == false )
		{
			kill( g_Child, SIGKILL );
			int status = 0;
			waitpid( g_Child, &status, 0 );
			g_ExitCode = -1;
		}
	}

#endif

	g_bExited = true;
	g_bStarted = false;
	CloseAll( );
}

//*****************************************************************************
//
int HostProcessExitCode( void )
{
	return static_cast<int>( g_ExitCode );
}

//*****************************************************************************
//
bool HostProcessExited( void )
{
	return g_bExited;
}

//*****************************************************************************
//
void HostProcessShutdown( void )
{
	HostProcessStop( );
}

} // namespace zx
