// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What hosting asks of the server registries the browser keeps, which the browser installs since hosting sits below it; before that every answer is none.

#ifndef ZX_REGISTRYDIRECTORY_H
#define ZX_REGISTRYDIRECTORY_H

struct NETADDRESS_s;

namespace zx
{

struct RegistryDirectory
{
	// [rc4l] The registry that last answered, else the first known.
	bool ( *address )( NETADDRESS_s &out );
	// [rc4l] A registry of one address family.
	bool ( *addressForFamily )( bool bIPv6, NETADDRESS_s &out );
	// [rc4l] The registry a server was listed with, remembered from the browse.
	bool ( *registryForServer )( const NETADDRESS_s &server, NETADDRESS_s &out );
	// [rc4l] A brokered punch's verdict arrived, so the rows waiting on one are tried.
	void ( *punchBrokered )( void );
};

void SetRegistryDirectory( const RegistryDirectory *directory );
const RegistryDirectory &Registries( void );

} // namespace zx

#endif // ZX_REGISTRYDIRECTORY_H
