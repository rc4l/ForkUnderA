// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] What has to happen before a remembered server can be started again.
//
// Starting the server is the easy half. The hard half is that WE have to be able to join it, and a
// client can only join a server holding the same files it is holding. Our own files are fixed for
// the life of the process, so when they differ from the ones the server will be started with, the
// join is refused before it begins -- and it is refused with a wall of names and hashes that says
// nothing about what to do next.
//
// The offline path has always known this: it compares the set, reloads the engine when it differs,
// and loads the save afterwards. The rehost path did not, so it spawned servers it could not reach.
// Same problem, so the same three answers.
//
// Order matters. Files that are not on this machine cannot be reloaded into, so missing beats
// different: there is no point restarting the engine to arrive at the same refusal with the map
// gone from under us.
//
// Header-pure by the features/ rules: no engine types.

#ifndef ZX_CONTINUEREHOST_COMPUTE_H
#define ZX_CONTINUEREHOST_COMPUTE_H

namespace zx
{

struct ContinueRehostInputs
{
	// Every file the remembered server needs was found on this machine. Without them there is
	// nothing to start and nothing to reload into.
	bool filesFound;

	// What we have loaded right now is what that server will be started with, so we can join it as
	// we are.
	bool filesMatchOurs;

	ContinueRehostInputs()
		: filesFound(false), filesMatchOurs(false) {}
};

enum class ContinueRehostStep
{
	Host,			// start it and join it, exactly as we are
	ReloadThenHost,	// restart the engine onto those files first, then start and join
	RefuseMissing,	// the files are gone, so say so rather than starting something unjoinable
};

ContinueRehostStep DecideContinueRehost(const ContinueRehostInputs &in);

} // namespace zx

#endif // ZX_CONTINUEREHOST_COMPUTE_H
