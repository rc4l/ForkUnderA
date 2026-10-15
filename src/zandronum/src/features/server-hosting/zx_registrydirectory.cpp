// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-hosting/zx_registrydirectory.h"

#include <stddef.h>

namespace zx
{

namespace
{

bool NoAddress( NETADDRESS_s & ) { return false; }
bool NoAddressForFamily( bool, NETADDRESS_s & ) { return false; }
bool NoRegistryForServer( const NETADDRESS_s &, NETADDRESS_s & ) { return false; }
void NoPunch( void ) { }

const RegistryDirectory g_None = { NoAddress, NoAddressForFamily, NoRegistryForServer, NoPunch };
const RegistryDirectory *g_Directory = &g_None;

} // namespace

void SetRegistryDirectory( const RegistryDirectory *directory ) { g_Directory = ( directory != NULL ) ? directory : &g_None; }

const RegistryDirectory &Registries( void ) { return *g_Directory; }

} // namespace zx
