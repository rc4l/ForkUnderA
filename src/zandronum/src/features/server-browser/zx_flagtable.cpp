// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_flagtable.h for why this is a walk of the engine's own cvars rather than a list.

#include "features/server-browser/zx_flagtable.h"
#include "features/server-browser/computation/flaghelp_compute.h"

#include <algorithm>

#include "c_cvars.h"
#include "c_dispatch.h"
#include "doomtype.h"
#include "v_text.h"

namespace zx
{

namespace
{

// Low bit first, so a row of switches reads in the order the number's bits do.
bool ByBit(const FlagBit &a, const FlagBit &b)
{
	return a.bit < b.bit;
}

} // namespace

std::vector<FlagField> FlagTable()
{
	std::vector<FlagField> fields;

	for (FBaseCVar *cvar = CVars; cvar != NULL; cvar = cvar->GetNext())
	{
		if (!cvar->IsFlagCVar())
			continue;

		FFlagCVar *const flag = static_cast<FFlagCVar *>(cvar);
		FIntCVar *const owner = flag->GetValueVar();
		if ((owner == NULL) || (owner->GetName() == NULL))
			continue;

		// [rc4l] SERVER FLAGS ONLY, and the test is the cvar's own CVAR_SERVERINFO rather than a
		// list of field names kept here.
		//
		// The walk finds every flag in the build, which is the point -- and that includes ones with
		// nothing to do with a server: paletteflash is how the player likes their screen to flash,
		// cl_clientflags is their own connection, gl_texture_hqresize_targets is their renderer.
		// Offering those on a screen that configures a server would be offering to set somebody
		// else's preferences.
		//
		// CVAR_SERVERINFO is exactly the line: it means the value is part of what the server tells
		// its clients. lmsspectatorsettings and sv_forbidvoteflags carry it and belong here;
		// paletteflash does not and does not.
		if ((owner->GetFlags() & CVAR_SERVERINFO) == 0)
			continue;

		const std::string fieldName = owner->GetName();

		size_t at = fields.size();
		for (size_t i = 0; i < fields.size(); ++i)
		{
			if (fields[i].name == fieldName)
			{
				at = i;
				break;
			}
		}

		if (at == fields.size())
		{
			FlagField created;
			created.name = fieldName;
			created.value = static_cast<unsigned int>(**owner);
			fields.push_back(created);
		}

		fields[at].bits.push_back(FlagBit(cvar->GetName(), flag->GetBitVal()));
	}

	for (size_t i = 0; i < fields.size(); ++i)
		std::sort(fields[i].bits.begin(), fields[i].bits.end(), ByBit);

	// Into the order they are always quoted in, which is the pure unit's answer rather than one
	// worked out again here.
	std::vector<std::string> found;
	for (size_t i = 0; i < fields.size(); ++i)
		found.push_back(fields[i].name);

	const std::vector<std::string> order = FlagFieldOrder(found);

	std::vector<FlagField> out;
	out.reserve(fields.size());

	for (size_t i = 0; i < order.size(); ++i)
	{
		for (size_t j = 0; j < fields.size(); ++j)
		{
			if (fields[j].name == order[i])
			{
				out.push_back(fields[j]);
				break;
			}
		}
	}

	return out;
}

void FlagTableApply(const std::string &field, unsigned int value)
{
	FBaseCVar *const cvar = FindCVar(field.c_str(), NULL);
	if (cvar == NULL)
		return;

	// [rc4l] Through the cvar rather than by writing the integer, so every callback Zandronum hangs
	// off these runs -- they are what keep the game state in step with the number.
	UCVarValue set;
	set.Int = static_cast<int>(value);
	cvar->SetGenericRep(set, CVAR_Int);
}

} // namespace zx

// [rc4l] The table, printed. This is how the walk was checked against the source: 146 flags across
// six fields in d_main.cpp, plus the two declared elsewhere and the field they brought with them.
CCMD( fua_flags )
{
	const std::vector<zx::FlagField> fields = zx::FlagTable( );

	int total = 0;
	for ( size_t i = 0; i < fields.size( ); ++i )
		total += static_cast<int>( fields[i].bits.size( ));

	Printf( "%d flag(s) across %d field(s)\n", total, static_cast<int>( fields.size( )));

	for ( size_t i = 0; i < fields.size( ); ++i )
	{
		const zx::FlagField &f = fields[i];

		Printf( TEXTCOLOR_GOLD "%s" TEXTCOLOR_NORMAL " = %s  (%d flags, %d unknown bit(s) set)\n",
			f.name.c_str( ), zx::FormatFlagNumber( f.value ).c_str( ),
			static_cast<int>( f.bits.size( )),
			zx::CountBits( zx::UnknownBits( f.value, f.bits )));

		if (( argv.argc( ) >= 2 ) && ( stricmp( argv[1], "all" ) == 0 ))
		{
			for ( size_t b = 0; b < f.bits.size( ); ++b )
			{
				Printf( "    %-40s %-5s %s\n", f.bits[b].name.c_str( ),
					zx::FlagIsOn( f.value, f.bits[b].bit ) ? "true" : "false",
					zx::FlagHelp( f.bits[b].name ));
			}
		}
	}

	// [rc4l] Anything the FLAGS box would show with nothing to say about it.
	//
	// The walk finds whatever this build has, and the help is a written table, so the two can only
	// drift one way: an engine update adds a flag and nobody notices it has no line. This says so.
	int quiet = 0;
	for ( size_t i = 0; i < fields.size( ); ++i )
	{
		for ( size_t b = 0; b < fields[i].bits.size( ); ++b )
		{
			if ( zx::FlagHelp( fields[i].bits[b].name )[0] != 0 )
				continue;

			if ( quiet == 0 )
				Printf( TEXTCOLOR_ORANGE "No description written for:\n" TEXTCOLOR_NORMAL );

			Printf( "    %s\n", fields[i].bits[b].name.c_str( ));
			quiet++;
		}
	}

	if ( quiet == 0 )
		Printf( TEXTCOLOR_GREEN "Every flag has a description.\n" TEXTCOLOR_NORMAL );
}
