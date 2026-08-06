// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/stableline_compute.h"

namespace zx
{

std::string MaskVarying( const std::string &text, char widest )
{
	std::string out( text );

	for ( size_t i = 0; i < out.size( ); ++i )
	{
		if ((( out[i] >= '0' ) && ( out[i] <= '9' )) || ( out[i] == ' ' ))
			out[i] = widest;
	}

	return out;
}

} // namespace zx
