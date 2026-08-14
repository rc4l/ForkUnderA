// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] One server process, owned by this one.
//
// THE ONLY PROMISE THAT MATTERS: when this process ends, by any route including a crash or a task
// manager kill, the child ends too. Everything else here is convenience; that is the correctness.
// An orphaned server holding a port and answering queries after the game is gone is the failure a
// player cannot diagnose and cannot forgive, and every platform needs a different mechanism to
// prevent it:
//
//   Windows  a job object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE. The handle dies with the process
//            whatever kills it, and the kernel takes the child with it.
//   Linux    prctl( PR_SET_PDEATHSIG ) in the child, between fork and exec.
//   macOS    no PDEATHSIG, so the child watches the parent instead: a kqueue NOTE_EXIT on the
//            parent pid, in a thread that exits the process when it fires.
//
// The output pipe is not a nicety either. We ask the child for -stdout and read it, because until a
// connection exists that pipe is the ONLY thing that can tell us why a server did not start -- and
// on Windows we deliberately hide the window that would otherwise have said so.

#ifndef ZX_HOSTPROCESS_H
#define ZX_HOSTPROCESS_H

#include <string>
#include <vector>

namespace zx
{

// Start `args` (element 0 is the executable) as a child we own, with its output on a pipe.
// Returns false and fills `outError` if it could not be created at all.
bool HostProcessStart( const std::vector<std::string> &args, std::string &outError );

// True while we hold a live child.
bool HostProcessRunning( void );

// Drain whatever the child has written since the last call. Never blocks. Returns "" when there is
// nothing waiting, which is the usual answer.
std::string HostProcessReadOutput( void );

// Ask it to stop, then make sure. Safe to call when nothing is running.
// [rc4l] BLOCKS the calling thread for up to three seconds while the child winds down -- which on
// the game loop is a beachball. Only for paths that cannot tick afterwards (process exit, and
// starting a replacement server that needs the old one's port). Interactive stops use
// HostProcessRequestStop and let HostTick reap the exit.
void HostProcessStop( void );

// [rc4l] Ask it to stop and return IMMEDIATELY -- no wait, no beachball. The pipe stays open so
// the tick keeps draining output, HostProcessRunning() keeps answering, and the ordinary
// child-exited path in HostTick observes the death and cleans up. Safe when nothing is running.
void HostProcessRequestStop( void );

// [rc4l] The escalation for a stop that timed out: kill it now, reap it now, close everything.
// The wait is momentary -- SIGKILL is not refusable -- so this is safe on the game loop.
void HostProcessKill( void );

// The child's exit code, meaningful only once it has exited.
int HostProcessExitCode( void );

// True once a child we started has gone. False while it runs and false when there never was one.
bool HostProcessExited( void );

// [rc4l] Registered with atterm so that even an orderly shutdown that forgets to stop hosting still
// takes the server with it. The job object covers the disorderly cases; this covers the tidy one.
void HostProcessShutdown( void );

} // namespace zx

#endif // ZX_HOSTPROCESS_H
