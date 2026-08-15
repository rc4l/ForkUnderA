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
#include "features/sky-tint/zx_skytint.h"
#include "g_level.h"  // G_DeferedInitNew
#include "w_wad.h"    // Wads.CheckNumForName
#include "gstrings.h"
#include "p_local.h"
#include "c_cvars.h"
#include "features/fua-caching/fua_caching.h" // GStrings, for the restart reset
#include "gi.h"       // DoomStartupInfo

#include "features/wadreload/zx_wadreload.h"
#include "features/wadreload/computation/wadreload_compute.h"

// [rc4l] Set by RequestReload before it throws CRestartException; consumed on the restart path in
// D_DoomMain to boot straight into a map instead of the title screen. Empty = go to title as usual.
FString StoredReloadMap;

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

// Open `path` as a resource archive and return true if it contains the boot palette (PLAYPAL) lump.
// Non-archives / unopenable files return false (they're rejected as not-loadable elsewhere anyway).
bool FileHasBootPalette(const char *path)
{
	if (path == NULL || path[0] == '\0')
		return false;

	FileReader *reader;
	try { reader = new FileReader(path); }
	catch (CRecoverableError &) { return false; }

	// OpenResourceFile takes ownership of the reader on success (freed by ~FResourceFile); on failure
	// it does not, so we delete it ourselves.
	FResourceFile *rf = FResourceFile::OpenResourceFile(path, reader, true /*quiet*/);
	if (rf == NULL)
	{
		delete reader;
		return false;
	}

	bool found = false;
	for (DWORD i = 0; i < rf->LumpCount() && !found; ++i)
	{
		FResourceLump *lump = rf->GetLump((int)i);
		if (lump == NULL)
			continue;
		// A WAD lump exposes its bare name; a pk3 entry also has a FullName ("PLAYPAL.pal", possibly
		// in a subdirectory). Either matching is enough -- IsBootPaletteName handles basename + ext.
		if (IsBootPaletteName(lump->Name) ||
		    (lump->FullName != NULL && IsBootPaletteName(lump->FullName)))
			found = true;
	}
	delete rf;
	return found;
}

// True if the resulting IWAD + pwad set can supply a palette, i.e. it will boot. A stub IWAD (e.g.
// MM8BDM's megagame.wad) has no palette on its own and must be paired with its content pk3.
bool SetHasBootPalette(const char *iwad, const TArray<FString> &pwads)
{
	if (FileHasBootPalette(iwad))
		return true;
	for (unsigned i = 0; i < pwads.Size(); ++i)
		if (FileHasBootPalette(pwads[i].GetChars()))
			return true;
	return false;
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

//==========================================================================
//
// [rc4l] ResetStartupStateForRestart -- see the header for why this is ours and lives here.
//
// Both items below are CORRECT for a normal boot. Neither owning function is changed: altering
// FStringTable::FreeNonDehackedStrings or the `if (Name.IsEmpty())` guard in
// FIWadManager::FindIWAD would change behaviour for everyone to fix a case only a restart reaches,
// and would leave a permanent conflict in vendored files we re-sync from upstream. Doing it from
// here costs one call site in d_main.cpp instead.
//
//==========================================================================

void ResetStartupStateForRestart()
{
	// 1. LoadStrings keeps DEHACKED-set entries -- PassNum 0 survives FreeNonDehackedStrings -- so a
	//    Dehacked patch beats a LANGUAGE lump. Right within one boot; wrong across a restart, where
	//    the patch that set them is no longer loaded. Freedoom's DEHACKED renames the weapons, so
	//    reloading onto doom2.wad kept Freedoom's weapon names on Doom's guns.
	//
	//    Clearing outright is safe HERE specifically: D_DoomMain calls GStrings.LoadStrings() and
	//    then D_LoadDehLumps() further down, so the new WAD set's own patches re-apply after this.
	GStrings.FlushAllForRestart();

	// 2. DoomStartupInfo is filled from GAMEINFO and again by FIWadManager::FindIWAD, both guarded on
	//    the field being empty, so the banner and title colours stayed on the PREVIOUS IWAD's: the
	//    engine loaded doom2.wad (2919 lumps, confirmed in the log) and still announced itself as
	//    Freedoom.
	DoomStartupInfo.Name = "";
	DoomStartupInfo.Song = "";
	DoomStartupInfo.FgColor = 0;
	DoomStartupInfo.BkColor = 0;
	DoomStartupInfo.Type = FStartupInfo::DefaultStartup;

	// 3. [rc4l] Flush caches that key on identities the rebuild invalidates: fua-caching's
	//    DECORATE reference table (PClass pointers -- this one crashed for real in
	//    GetReplacement), the FindStateByString memo (ClassIndex + FState pointers), and the
	//    FindCVar memo (CVARINFO cvars are destroyed and recreated). Stale entries here would
	//    hand the new WAD set pointers into the old one.
	FUA_CachingReset();
	P_ClearStateStringCache();
	C_ClearCVarCache();

	// 4. features/sky-tint keeps a per-SUBSECTOR colour table for the level it was built on. A
	//    restart happens inside the same process, so that table outlives the sector and subsector
	//    arrays it indexes: the next level then reads another map's colours at its own indices, and
	//    since the table said "yes there is a tint" it kept saying so after leaving the map that
	//    made it. Reported as sky tint dying on a mod's map and staying dead afterwards.
	//
	//    Cleared rather than rebuilt: P_SetupLevel rebuilds it for whatever loads next, and there is
	//    no level to build it from at this point anyway.
	zx::SkyTint_Clear();
}

ReloadResult RequestReload(const char *iwad, const TArray<FString> &pwads, const char *startMap,
	const char *connectAddress)
{
	const bool changingIwad = (iwad != NULL && iwad[0] != '\0');
	const bool haveMap = (startMap != NULL && startMap[0] != '\0');
	const bool haveConnect = (connectAddress != NULL && connectAddress[0] != '\0');

	// 1. Skip the full re-init when the wanted set already matches what's loaded. If a map or a server
	//    was asked for, still honor it -- just switch/connect directly (no teardown needed).
	const FString        curIwadFS = CurrentIwad();
	const TArray<FString> curFiles = CurrentFiles();
	const FString wantIwadFS = changingIwad ? FString(iwad) : curIwadFS; // empty iwad => keep current
	if (WantedMatchesLoaded(curIwadFS.GetChars(), ToStd(curFiles),
	                        wantIwadFS.GetChars(), ToStd(pwads)))
	{
		if (haveConnect)
		{
			// [rc4l] Already running the server's WAD set, so joining costs nothing: connect in place
			// rather than restarting the engine to arrive at the same state. This is the common case
			// for hopping between servers on the same mod, and the difference the player sees is a
			// couple of seconds against a full re-init.
			Printf("wad_reload: WAD set unchanged; connecting to %s.\n", connectAddress);
			FString cmd;
			cmd.Format("connect %s", connectAddress);
			C_DoCommand(cmd.GetChars());
		}
		else if (haveMap)
		{
			Printf("wad_reload: WAD set unchanged; switching to map %s.\n", startMap);
			G_DeferedInitNew(startMap);
		}
		else
		{
			Printf("wad_reload: the requested WAD set is already loaded; nothing to do.\n");
		}
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
	// A file being a valid WAD is not enough: the set must actually BOOT. The one failure that a
	// validated-but-unbootable set hits first is a missing palette (V_Init -> "PLAYPAL not found" ->
	// I_FatalError), which is exactly what a *stub* IWAD like MM8BDM's megagame.wad does when reloaded
	// without its content pk3. Catch it here, BEFORE tearing down the running game, and refuse -- same
	// rollback contract as a missing file. Only meaningful when we're changing the IWAD; keeping the
	// current one means the palette that is already booted stays put.
	if (ok && changingIwad && !SetHasBootPalette(iwad, pwads))
	{
		Printf(TEXTCOLOR_RED "wad_reload: '%s' can't run on its own -- it needs its other game files "
		       "loaded with it.\n", iwad);
		Printf(TEXTCOLOR_RED "wad_reload: add them too, e.g. wad_reload %s <otherfile.pk3>.\n", iwad);
		ok = false;
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
	// [rc4l] -connect is in the strip list above precisely so a plain reload cannot silently rejoin
	// the last server; putting it back here is what turns this call into "join". The engine's normal
	// startup consumes it, so nothing extra has to survive the restart.
	if (haveConnect) { append.push_back("-connect"); append.push_back(connectAddress); }

	std::vector<std::string> newArgv = ComputeReloadArgv(curArgv, remove, append);

	Args->FlushArgs();
	for (size_t i = 0; i < newArgv.size(); ++i)
		Args->AppendArg(FString(newArgv[i].c_str()));

	// Boot straight into this map after the restart (consumed in D_DoomMain's restart path), or leave
	// empty to land on the title screen as a restart normally does.
	StoredReloadMap = haveMap ? FString(startMap) : FString("");

	if (haveConnect)
		Printf("wad_reload: restarting onto the new WAD set, connecting to %s...\n", connectAddress);
	else if (haveMap)
		Printf("wad_reload: restarting onto the new WAD set, into map %s...\n", startMap);
	else
		Printf("wad_reload: restarting onto the new WAD set...\n");
	throw CRestartException();
	// not reached
}

}} // namespace zx::wadreload

//==========================================================================
//
// CCMD wad_reload
//
// wad_reload <iwad|-> [pwad ...] [map=<MAP>]
//   Reloads the engine onto a new WAD set at runtime. The first argument is the IWAD path, or "-"
//   to keep the current IWAD; the rest replace the -file set. A trailing map=<MAP> token (e.g.
//   map=MAP11) boots straight into that map instead of the title screen. Validates the set first and
//   refuses (leaving the game running) if anything is missing or not a real WAD.
//
//==========================================================================

CCMD(wad_reload)
{
	if (argv.argc() < 2)
	{
		Printf("usage: wad_reload <iwad|-> [pwad ...] [map=<MAP>]\n"
		       "  first arg is the IWAD path, or - to keep the current IWAD; the rest replace -file;\n"
		       "  an optional map=MAP01 boots straight into that map after the reload.\n");
		return;
	}

	const char *iwad = argv[1];
	const bool keepIwad = (strcmp(iwad, "-") == 0 || strcmp(iwad, ".") == 0);

	// Pull an optional "map=<MAP>" token out of the arg list; everything else is a pwad.
	FString startMap;
	TArray<FString> pwads;
	for (int i = 2; i < argv.argc(); ++i)
	{
		std::string mapVal = zx::wadreload::ParseMapAssignment(argv[i]);
		if (!mapVal.empty())
			startMap = mapVal.c_str();
		else
			pwads.Push(argv[i]);
	}

	zx::wadreload::RequestReload(keepIwad ? NULL : iwad, pwads,
	                             startMap.IsNotEmpty() ? startMap.GetChars() : NULL);
}
