// [rc4l] See zx_wadreload.h. Engine glue around the pure decisions in computation/wadreload_compute.*
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include <sys/types.h>
#include <sys/stat.h>

#include "doomtype.h"
#include "m_argv.h"
#include "cmdlib.h"
#include "c_dispatch.h"
#include "v_text.h"
#include "files.h"
#include "doomerrors.h"
#include "resourcefiles/resourcefile.h"
#include "d_main.h"   // CRestartException

#include "features/wadreload/zx_wadreload.h"
#include "features/wadreload/computation/wadreload_compute.h"

namespace zx { namespace wadreload {

namespace {

// Pull the current -iwad value and the -file run out of the global Args (what's loaded right now).
FString CurrentIwad()
{
	const char *iw = Args->CheckValue("-iwad");
	return FString(iw != NULL ? iw : "");
}

TArray<FString> CurrentFiles()
{
	TArray<FString> out;
	FString *files;
	int n = Args->CheckParmList("-file", &files);
	for (int i = 0; i < n; ++i)
		out.Push(files[i]);
	return out;
}

std::vector<std::string> ToStd(const TArray<FString> &a)
{
	std::vector<std::string> v;
	for (unsigned i = 0; i < a.Size(); ++i)
		v.push_back(a[i].GetChars());
	return v;
}

} // namespace

bool WadLoadable(const char *path, FString &outWhy)
{
	if (path == NULL || path[0] == '\0')
	{
		outWhy = "empty path";
		return false;
	}

	struct stat info;
	if (stat(path, &info) != 0)
	{
		outWhy = "file not found";
		return false;
	}

	// A directory-as-mod is loaded via OpenDirectory (mirrors AddFile).
	if (info.st_mode & S_IFDIR)
	{
		FResourceFile *dir = FResourceFile::OpenDirectory(path);
		if (dir == NULL) { outWhy = "not a readable directory mod"; return false; }
		delete dir;
		return true;
	}

	// A file: it must be a recognized ARCHIVE container (WAD/PK3/PK7). We deliberately do NOT use
	// FResourceFile::OpenResourceFile here -- its format table ends in a catch-all that wraps any file
	// (including a truncated or garbage download) as a single lump, so it never rejects. For a reload
	// gate we want the opposite: refuse anything that isn't a real archive. Read the leading bytes and
	// classify by magic.
	FileReader *reader;
	try
	{
		reader = new FileReader(path);
	}
	catch (CRecoverableError &)
	{
		outWhy = "could not open file";
		return false;
	}

	unsigned char magic[8] = { 0 };
	long got = 0;
	try
	{
		reader->Seek(0, SEEK_SET);
		got = reader->Read(magic, (long)sizeof(magic));
	}
	catch (CRecoverableError &) { got = 0; }
	delete reader;

	if (ClassifyArchiveMagic(magic, got > 0 ? (size_t)got : 0) == ArchiveKind::Unknown)
	{
		outWhy = "not a WAD/PK3/PK7 archive (corrupt or wrong file?)";
		return false;
	}
	return true;
}

ReloadResult RequestReload(const char *iwad, const TArray<FString> &pwads)
{
	const bool changingIwad = (iwad != NULL && iwad[0] != '\0');

	// 1. Skip when the wanted set already matches what's loaded (no pointless full re-init).
	const FString        curIwadFS = CurrentIwad();
	const TArray<FString> curFiles = CurrentFiles();
	const FString wantIwadFS = changingIwad ? FString(iwad) : curIwadFS; // empty iwad => keep current
	if (WantedMatchesLoaded(curIwadFS.GetChars(), ToStd(curFiles),
	                        wantIwadFS.GetChars(), ToStd(pwads)))
	{
		Printf("wad_reload: the requested WAD set is already loaded; nothing to do.\n");
		return ReloadResult::AlreadyLoaded;
	}

	// 2. Validate the whole wanted set BEFORE tearing anything down. If any file is not loadable,
	//    refuse and leave the running game untouched (this is the rollback -- we never commit a set
	//    that would boot missing files or hard-fail).
	bool ok = true;
	if (changingIwad)
	{
		FString why;
		if (!WadLoadable(iwad, why))
		{
			Printf(TEXTCOLOR_RED "wad_reload: IWAD '%s' is not loadable (%s).\n", iwad, why.GetChars());
			ok = false;
		}
	}
	for (unsigned i = 0; i < pwads.Size(); ++i)
	{
		FString why;
		if (!WadLoadable(pwads[i].GetChars(), why))
		{
			Printf(TEXTCOLOR_RED "wad_reload: file '%s' is not loadable (%s).\n", pwads[i].GetChars(), why.GetChars());
			ok = false;
		}
	}
	if (!ok)
	{
		Printf(TEXTCOLOR_RED "wad_reload: aborted -- keeping the current WAD set.\n");
		return ReloadResult::InvalidWads;
	}

	// 3. Rewrite Args to the new set using the tested pure helper (avoids DArgs::RemoveArgs' tail bug),
	//    then throw CRestartException so D_DoomMain re-reads the WAD set and comes back up on it.
	std::vector<std::string> curArgv;
	for (int i = 0; i < Args->NumArgs(); ++i)
	{
		const char *a = Args->GetArg(i);
		curArgv.push_back(a != NULL ? a : "");
	}

	// Switches to drop: always the file set; the IWAD only when we're replacing it (empty iwad keeps
	// the current one); and demo/save/connect switches that must not survive into a fresh WAD set.
	std::vector<std::string> remove = { "-file", "-playdemo", "-timedemo", "-record", "-loadgame", "-connect" };
	if (changingIwad) remove.push_back("-iwad");

	std::vector<std::string> append;
	if (changingIwad) { append.push_back("-iwad"); append.push_back(iwad); }
	if (pwads.Size() > 0)
	{
		append.push_back("-file");
		for (unsigned i = 0; i < pwads.Size(); ++i)
			append.push_back(pwads[i].GetChars());
	}

	std::vector<std::string> newArgv = ComputeReloadArgv(curArgv, remove, append);

	Args->FlushArgs();
	for (size_t i = 0; i < newArgv.size(); ++i)
		Args->AppendArg(FString(newArgv[i].c_str()));

	Printf("wad_reload: restarting onto the new WAD set...\n");
	throw CRestartException();
	// not reached
}

}} // namespace zx::wadreload

//==========================================================================
//
// CCMD wad_reload
//
// wad_reload <iwad|-> [pwad ...]
//   Reloads the engine onto a new WAD set at runtime. The first argument is the IWAD path, or "-"
//   to keep the current IWAD; the rest replace the -file set. Validates the set first and refuses
//   (leaving the game running) if anything is missing or not a real WAD.
//
//==========================================================================

CCMD(wad_reload)
{
	if (argv.argc() < 2)
	{
		Printf("usage: wad_reload <iwad|-> [pwad ...]\n"
		       "  first arg is the IWAD path, or - to keep the current IWAD; the rest replace -file.\n");
		return;
	}

	const char *iwad = argv[1];
	const bool keepIwad = (strcmp(iwad, "-") == 0 || strcmp(iwad, ".") == 0);

	TArray<FString> pwads;
	for (int i = 2; i < argv.argc(); ++i)
		pwads.Push(argv[i]);

	zx::wadreload::RequestReload(keepIwad ? NULL : iwad, pwads);
}
