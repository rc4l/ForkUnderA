/*
** stringtable.h
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
**
** FStringTable
**
** This class manages a list of localizable strings stored in a wad file.
*/

#ifndef __STRINGTABLE_H__
#define __STRINGTABLE_H__

#ifdef _MSC_VER
#pragma once
#endif


#include <stdlib.h>
#include <atomic>
#include "doomtype.h"

class FStringTable
{
public:
	FStringTable ();
	~FStringTable ();
	FStringTable (const FStringTable &) = delete;
	FStringTable &operator= (const FStringTable &) = delete;

	void LoadStrings (bool enuOnly);

	const char *operator() (const char *name) const;	// Never returns NULL
	const char *operator[] (const char *name) const;	// Can return NULL

	const char *MatchString (const char *string) const;
	void SetString (const char *name, const char *newString);

	// [Doruk-Aksoy] uzdoom@f2d7bbea99: the gender LMACROS texts are read for, the local player's, set once a frame.
	void SetDefaultGender (int gender) { DefaultGender.store (gender, std::memory_order_relaxed); }

	// [rc4l] Drop EVERYTHING, including the DEHACKED-set entries that LoadStrings deliberately
	// preserves. Only the engine restart path wants this -- see the restart block in D_DoomMain for
	// why. Additive wrapper rather than making FreeData public, so re-syncing this header stays a
	// clean merge.
	void FlushAllForRestart () { FreeData (); }

private:
	// [Doruk-Aksoy] features/language-lumps' LanguageTable, kept out of this widely included header.
	struct Table;
	Table *Strings;
	std::atomic<int> DefaultGender;

	void FreeData ();
	void FreeNonDehackedStrings ();
};

#endif //__STRINGTABLE_H__
