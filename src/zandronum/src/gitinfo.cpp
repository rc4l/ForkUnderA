/*
** gitinfo.cpp
** Returns strings from gitinfo.h.
**
**---------------------------------------------------------------------------
** Copyright 2013 Randy Heit
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
** This file is just here so that when gitinfo.h changes, only one source
** file needs to be recompiled.
*/

#include "gitinfo.h"
#include "version.h"

const char *GetGitDescription()
{
	// [BB]
	return HG_REVISION_HASH_STRING; // GIT_DESCRIPTION;
}

const char *GetGitHash()
{
	// [BB]
	return HG_REVISION_HASH_STRING; // GIT_HASH;
}

const char *GetGitTime()
{
	// [BB]
	return HG_TIME; // GIT_TIME;
}

// [rc4l] Our own version, straight from `git describe` -- e.g. "v0.1.19-29-gde55d35". Note this is
// NOT GetGitDescription(): that one is overridden above to return the 12-char hash, which the crash
// reporter uses to build the "ZandroX@<sha>" release id that symbol assets are published under.
// Repointing it at the describe string would silently break crash symbolication.
const char *GetFuaDescribe()
{
	return GIT_DESCRIPTION;
}

const char *GetVersionString()
{
	// [BB]
	//if (GetGitDescription()[0] == '\0')
	{
		return VERSIONSTR;
	}
	/*
	else
	{
		return GIT_DESCRIPTION;
	}
	*/
}

// [BB]
const char *GetVersionStringRev()
{
	//FString s = DOTVERSIONSTR "-r" HG_TIME;
	//return s.GetChars();
	return DOTVERSIONSTR_REV;
}

// [BB]
int GetRevisionNumber()
{
	return HG_REVISION_NUMBER;
}
