// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/registrymemory_compute.h"

namespace zx
{

// [rc4l] Out of line because C++14 needs a definition once anything binds it to a reference, which
// the tests do.
const size_t RegistryMemory::kCapacity;

void RegistryMemory::Remember( const std::string &server, const std::string &registry )
{
	// An address we cannot name is one we could never look up again.
	if ( server.empty( ) || registry.empty( ) )
		return;

	for ( size_t i = 0; i < m_Rows.size( ); ++i )
	{
		if ( m_Rows[i].server == server )
		{
			m_Rows[i].registry = registry;
			return;
		}
	}

	// Oldest out, on the reasoning that the servers worth reconnecting to are the recent ones.
	if ( m_Rows.size( ) >= kCapacity )
		m_Rows.erase( m_Rows.begin( ));

	Row row;
	row.server = server;
	row.registry = registry;
	m_Rows.push_back( row );
}

bool RegistryMemory::Recall( const std::string &server, std::string &out ) const
{
	for ( size_t i = 0; i < m_Rows.size( ); ++i )
	{
		if ( m_Rows[i].server == server )
		{
			out = m_Rows[i].registry;
			return true;
		}
	}

	return false;
}

RegistryChoice ChooseRegistry( bool haveRemembered, bool haveAnswering )
{
	if ( haveRemembered )
		return RegistryChoice::Remembered;

	return haveAnswering ? RegistryChoice::Answering : RegistryChoice::None;
}

} // namespace zx
