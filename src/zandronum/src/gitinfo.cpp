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

#include <string.h>

#include "gitinfo.h"
#include "version.h"
#include "features/fua-branding/computation/fua_version_compute.h"

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

// [rc4l] The version THIS engine is, as a player would name it: "v0.1.29".
//
// GetVersionStringRev() returns Zandronum's ("3.2.1-r36"), which is the wrong answer to every
// question a ZandroX player asks -- it is the same for every ZandroX release ever made, so it cannot
// tell you whether a server will let you in, and the server browser was showing it as though it were
// meaningful.
//
// The release tag only, with the commit-distance suffix from git describe stripped: two builds off
// the same release are compatible, and showing "v0.1.29-21-g249fc98" in a column would be noise.
const char *GetFuaVersionTag()
{
	static char tag[64] = { 0 };

	if ( tag[0] == '\0' )
	{
		zx::FuaVersionTag( GetFuaDescribe(), tag, sizeof tag );

		// An untagged build (shallow clone, fresh fork with no releases). Say so plainly rather than
		// claiming a version we do not have.
		if ( tag[0] == '\0' )
			strncpy( tag, "unversioned", sizeof tag - 1 );
	}

	return tag;
}

// [BB]
int GetRevisionNumber()
{
	return HG_REVISION_NUMBER;
}
