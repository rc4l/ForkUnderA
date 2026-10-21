// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Which registry told us about which server, so a punch is asked of one that can answer.
//
// Asking for an introduction means asking a registry about a server it holds. We were asking
// whichever registry answered a list query most recently, which is right the instant a browser
// refresh finishes and steadily less right afterwards. Reconnect is the case where it is simply
// wrong: after the join reload there has been no refresh, so there is no answering registry and the
// ask falls back to whichever address resolved first. A registry with no entry for that server can
// only say NotListed, so the punch never happens and the reconnect fails exactly as it did before.
//
// The federation makes this ordinary rather than exotic. A player with three registries configured
// is a player whose servers came from three different places, and the one that listed a server is
// the only one that can introduce anybody to it.
//
// This survives the join reload without being written anywhere: RequestReload throws
// CRestartException, which unwinds and re-runs startup inside the SAME process, so a static keeps
// its value across it. Two copies of the engine are two processes with two tables, which is also why
// there is no file to collide over and no per-instance suffix to manage.
//
// Bounded, because a browser refresh can carry hundreds of servers and a player may refresh all
// evening. The oldest goes when it is full, on the reasoning that the servers worth reconnecting to
// are the ones seen recently.
//
// Header-pure by the features/ rules: no engine types, so addresses arrive as the strings the
// engine already formats them into.

#ifndef ZX_REGISTRYMEMORY_COMPUTE_H
#define ZX_REGISTRYMEMORY_COMPUTE_H

#include <cstddef>
#include <string>
#include <vector>

namespace zx
{

class RegistryMemory
{
public:
	// One row per server the browser has ever been told about, capped. 512 is the browser's own
	// server limit, so a full list fits with nothing evicted.
	static const size_t kCapacity = 512;

	// Record that `registry` listed `server`. Seeing the same server again updates which registry
	// listed it, in place, rather than growing: a server that appears in every refresh must not
	// evict everything else.
	void Remember( const std::string &server, const std::string &registry );

	// The registry that listed `server`, or false if we were never told.
	bool Recall( const std::string &server, std::string &out ) const;

	size_t Size( ) const { return m_Rows.size( ); }

	void Clear( ) { m_Rows.clear( ); }

private:
	struct Row
	{
		std::string server;
		std::string registry;
	};

	std::vector<Row> m_Rows;
};

// Which registry a punch for this target should be asked of.
enum class RegistryChoice
{
	Remembered,	// the one that listed it, which is the only one that can answer about it
	Answering,	// nothing remembered, so fall back to whichever registry last answered
	None,		// no registry to ask at all, so do not ask
};

RegistryChoice ChooseRegistry( bool haveRemembered, bool haveAnswering );

} // namespace zx

#endif // ZX_REGISTRYMEMORY_COMPUTE_H
