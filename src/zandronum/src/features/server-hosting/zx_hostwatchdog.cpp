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

#include "features/server-hosting/zx_hostwatchdog.h"

#include "doomtype.h"
#include "c_dispatch.h"
#include "m_argv.h"

#ifndef _WIN32
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#endif

#include <stdlib.h>

namespace zx
{

namespace
{

// How often to look. Two seconds is imperceptible to a player who has already quit, and 30 wakeups a
// minute is nothing next to a server ticking at 35Hz.
const int kPollMs = 2000;

#ifdef _WIN32

DWORD WINAPI WatchParent( LPVOID param )
{
	const DWORD parentPid = static_cast<DWORD>( reinterpret_cast<uintptr_t>( param ));

	// SYNCHRONIZE is all we need, and asking for no more means this still works if the parent is
	// running at a different integrity level.
	HANDLE hParent = OpenProcess( SYNCHRONIZE, FALSE, parentPid );
	if ( hParent == NULL )
	{
		// It is already gone, or we cannot see it. Either way there is nobody to serve.
		TerminateProcess( GetCurrentProcess( ), 0 );
		return 0;
	}

	WaitForSingleObject( hParent, INFINITE );
	CloseHandle( hParent );

	// Not exit( ) -- that would run atexit handlers on a thread that does not own them, in a process
	// whose reason for existing has just disappeared.
	TerminateProcess( GetCurrentProcess( ), 0 );
	return 0;
}

#else

void *WatchParent( void *param )
{
	const pid_t parentPid = static_cast<pid_t>( reinterpret_cast<intptr_t>( param ));

	for ( ;; )
	{
		// [rc4l] Two questions, because either alone is wrong. getppid( ) == 1 catches being
		// reparented to init, which is the usual signal; kill( pid, 0 ) catches the case where the
		// parent died and its pid was reused, which reparenting alone would miss on a busy machine.
		if ( getppid( ) != parentPid )
			break;
		if (( kill( parentPid, 0 ) != 0 ) && ( errno == ESRCH ))
			break;

		usleep( kPollMs * 1000 );
	}

	_exit( 0 );
	return NULL;
}

#endif

} // namespace

//*****************************************************************************
//
void HostWatchdogInit( void )
{
	const int arg = Args->CheckParm( "-fua_hostparent" );
	if (( arg == 0 ) || ( arg + 1 >= Args->NumArgs( )))
		return;

	const long parentPid = atol( Args->GetArg( arg + 1 ));
	if ( parentPid <= 0 )
		return;

#ifdef _WIN32

	DWORD threadId = 0;
	HANDLE hThread = CreateThread( NULL, 0, WatchParent,
		reinterpret_cast<LPVOID>( static_cast<uintptr_t>( parentPid )), 0, &threadId );
	if ( hThread != NULL )
		CloseHandle( hThread );

#else

	pthread_t thread;
	pthread_attr_t attr;
	pthread_attr_init( &attr );
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
	pthread_create( &thread, &attr, WatchParent,
		reinterpret_cast<void *>( static_cast<intptr_t>( parentPid )));
	pthread_attr_destroy( &attr );

#endif
}

} // namespace zx
