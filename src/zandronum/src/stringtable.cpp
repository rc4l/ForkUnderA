/*
** stringtable.cpp
** Implements the FStringTable class
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "stringtable.h"
#include "cmdlib.h"
#include "m_swap.h"
#include "w_wad.h"
#include "i_system.h"
#include "sc_man.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "v_text.h"
#include "gi.h"
#include "features/language-lumps/computation/languagetable_compute.h"

// [Doruk-Aksoy] features/language-lumps: the parsing and the table's rules are Compute* helpers, this file reads the lumps.
using namespace zx::language;

static_assert (kLanguageColorEscape == TEXTCOLOR_ESCAPE, "a LANGUAGE colour escape must be the text colour escape");
static_assert (ComputeLanguageCode ('e','n','u') == MAKE_ID ('e','n','u',0), "LANGUAGE codes pack as LanguageIDs does");

EXTERN_CVAR (Bool, developer)

// [Doruk-Aksoy] uzdoom@d453019a61: how much of a missing label the developer log prints.
CVAR (Int, language_debug_maxlen, 64, 0)

struct FStringTable::Table
{
	LanguageTable Entries;
};

FStringTable::FStringTable ()
	: Strings (new Table), DefaultGender (0)
{
}

FStringTable::~FStringTable ()
{
	delete Strings;
}

void FStringTable::FreeData ()
{
	Strings->Entries.clear ();
}

void FStringTable::FreeNonDehackedStrings ()
{
	ComputeLanguageClearLoaded (Strings->Entries);
}

// [Doruk-Aksoy] Replaces a string's '@[macro]'s with their gender texts and keeps it if its pass and file let it.
static void InsertString (LanguageTable &table, const std::string &name, const std::string &text, int passnum, int filenum, bool isDefault, const LanguageMacros &macros, bool report)
{
	const LanguageMacroResult result = ComputeSubstituteLanguageMacros (text, macros);
	if (result.badMacro && report) Printf ("Bad macro in LANGUAGE string %s\n", name.c_str ());
	ComputeLanguageInsert (table, name, result.strings, passnum, filenum, isDefault);
}

// [Doruk-Aksoy] One pass over a text LANGUAGE lump, keeping the strings whose header names the pass's language.
static void LoadLanguage (LanguageTable &table, int lumpnum, int filenum, const LanguagePass &pass, int passnum, const LanguageMacros &macros, bool report)
{
	static bool binarydone = false;
	bool sawHeader = false;
	bool isDefault = false;
	bool skipSection = true;

	FScanner sc (lumpnum);
	sc.SetCMode (true);
	while (sc.GetString ())
	{
		if (sc.Compare ("["))
		{ // Process language identifiers
			std::vector<std::string> tokens;
			sc.MustGetString ();
			while (!sc.Compare ("]"))
			{
				tokens.push_back (std::string (sc.String, sc.StringLen));
				sc.MustGetString ();
			}
			const LanguageHeader header = ComputeLanguageHeader (tokens);
			if (header.emptyCode)
			{
				sc.ScriptError ("The language code may not be empty.");
			}
			if (header.tilde && report && passnum == 1)
			{
				sc.ScriptMessage ("Deprecated option '~' found in language list");
			}
			sawHeader = true;
			isDefault = header.isDefault;
			skipSection = !ComputeLanguageHeaderMatches (header, pass.code, pass.exact);
			continue;
		}

		// Process string definitions.
		if (!sawHeader)
		{
			// LANGUAGE lump is bad. We need to check if this is an old binary
			// lump and if so just skip it to allow old WADs to run which contain
			// such a lump.
			if (!sc.isText ())
			{
				if (!binarydone) Printf ("Skipping binary 'LANGUAGE' lump.\n");
				binarydone = true;
				return;
			}
			// [Doruk-Aksoy] uzdoom@cec46eccb8: the lump is skipped rather than ending the game.
			if (report && passnum == 1) Printf (TEXTCOLOR_RED "Found a string without a language specified in %s.\n", Wads.GetLumpFullPath (lumpnum).GetChars ());
			return;
		}

		bool skip = skipSection;
		if (sc.Compare ("$"))
		{
			sc.MustGetStringName ("ifgame");
			sc.MustGetStringName ("(");
			sc.MustGetString ();
			skip |= !sc.Compare (GameTypeName ());
			sc.MustGetStringName (")");
			sc.MustGetString ();
		}

		const std::string name (sc.String);
		sc.MustGetStringName ("=");
		sc.MustGetString ();
		std::string text;
		do
		{
			if (!skip) text.append (sc.String, ComputeLanguageEscapes (sc.String));
			sc.MustGetString ();
		}
		while (!sc.Compare (";"));

		if (!skip) InsertString (table, name, text, passnum, filenum, isDefault, macros, report);
	}
}

// [Doruk-Aksoy] uzdoom@c2c9126453: a CSV LANGUAGE lump, parsed once and applied for every pass.
static void LoadLanguageCSV (LanguageTable &table, int lumpnum, int filenum, const char *buffer, size_t size, const std::vector<LanguagePass> &passes, const LanguageMacros &macros, bool report)
{
	const std::vector<std::vector<std::string>> rows = ComputeParseLanguageCSV (buffer, size);
	const LanguageCSVLayout layout = ComputeLanguageCSVLayout (rows[0]);
	if (layout.labelColumn < 0)
	{
		if (report) Printf (TEXTCOLOR_RED "%s has no 'identifier' column.\n", Wads.GetLumpFullPath (lumpnum).GetChars ());
		return;
	}

	for (size_t i = 0; i < passes.size (); ++i)
	{
		const int passnum = int (i) + 1;
		for (const LanguageCSVOp &op : ComputeLanguageCSVOps (rows, layout, passes[i].code, passes[i].exact, GameTypeName ()))
		{
			switch (op.kind)
			{
			case LanguageCSVOp::Set:		InsertString (table, op.label, op.text, passnum, filenum, layout.isDefault, macros, report); break;
			case LanguageCSVOp::Remove:		ComputeLanguageRemove (table, op.label, passnum); break;
			case LanguageCSVOp::PurgeOlder:	ComputeLanguagePurgeOlder (table, op.label, filenum); break;
			}
		}
	}
}

void FStringTable::LoadStrings (bool enuOnly)
{
	int lastlump, lump;
	const bool report = !enuOnly;

	FreeNonDehackedStrings ();

	// [Doruk-Aksoy] uzdoom@ce1ecd0d4b: LMACROS lumps give the gender texts an '@[name]' in a string stands for.
	LanguageMacros macros;
	lastlump = 0;
	while ((lump = Wads.FindLump ("LMACROS", &lastlump)) != -1)
	{
		FMemLump data = Wads.ReadLump (lump);
		ComputeReadLanguageMacros (macros, (const char *)data.GetMem (), data.GetSize ());
	}

	const std::vector<LanguagePass> passes = ComputeLanguagePasses (LanguageIDs, enuOnly);
	lastlump = 0;
	while ((lump = Wads.FindLump ("LANGUAGE", &lastlump)) != -1)
	{
		const int filenum = Wads.GetLumpFile (lump);
		FMemLump data = Wads.ReadLump (lump);
		const char *buffer = (const char *)data.GetMem ();
		if (ComputeIsLanguageCSV (buffer, data.GetSize ()))
		{
			LoadLanguageCSV (Strings->Entries, lump, filenum, buffer, data.GetSize (), passes, macros, report);
			continue;
		}
		for (size_t i = 0; i < passes.size (); ++i)
		{
			LoadLanguage (Strings->Entries, lump, filenum, passes[i], int (i) + 1, macros, report);
		}
	}
}

// [Doruk-Aksoy] uzdoom@a7c9e29ae8: under developer, each missing label is logged once.
static void ReportMissingString (const char *name)
{
	static std::mutex lock;
	static std::set<std::string> reported;

	const int maxlen = language_debug_maxlen;
	const LanguageMissingReport report = ComputeLanguageMissingReport (name, size_t (maxlen < 0 ? 0 : maxlen));
	{
		std::lock_guard<std::mutex> guard (lock);
		if (!reported.insert (report.key).second) return;
	}
	DPrintf ("%s\n", report.message.c_str ());
}

// Finds a string by name and returns its value
const char *FStringTable::operator[] (const char *name) const
{
	return ComputeLanguageFind (Strings->Entries, name, ComputeLanguageGenderIndex (DefaultGender.load (std::memory_order_relaxed)));
}

// Finds a string by name and returns its value. If the string does
// not exist, returns the passed name instead.
const char *FStringTable::operator() (const char *name) const
{
	const char *str = operator[] (name);
	if (str == NULL && name != NULL && developer) ReportMissingString (name);
	return str ? str : name;
}

// Find a string with the same exact text. Returns its name.
const char *FStringTable::MatchString (const char *string) const
{
	return ComputeLanguageMatch (Strings->Entries, string);
}

void FStringTable::SetString (const char *name, const char *newString)
{
	ComputeLanguageSet (Strings->Entries, name, newString);
}
