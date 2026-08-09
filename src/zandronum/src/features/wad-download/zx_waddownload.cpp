// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_waddownload.h for the design and where it came from.
//
// Threading follows features/updater exactly, for the same reason: the transfer blocks for minutes,
// so it runs on a detached worker, and the worker touches NOTHING the engine considers single-
// threaded -- no Printf, no CVARs, no FString, no wad tables. Everything it needs is snapshotted into
// a Job before it starts, and everything it wants to say goes into a mutex-guarded queue that Tick()
// drains on the main thread. Printf off the main thread has already crashed this engine once (see
// zx_updater.h); it is not a rule to relax.

#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
  #include <direct.h>
  #define zx_rmdir _rmdir
#else
  #include <unistd.h>
  #define zx_rmdir rmdir
#endif

#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "c_console.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "cmdlib.h"
#include "doomtype.h"
#include "gameconfigfile.h"
#include "i_system.h"		// [rc4l] findstate_t / I_Find* -- walking the store to prune it
#include "m_misc.h"
#include "v_text.h"

// [rc4l] kDefaultDownloadSites, generated from the repo-root waddownloadsites.txt.
#include "zx_waddownload_lists.h"

#include "features/net/zx_httpfile.h"
#include "features/wad-download/zx_filehash.h"
#include "features/wad-download/zx_wadsearch.h"
#include "features/wad-download/zx_waddownload.h"
#include "features/wad-download/computation/downloadplan_compute.h"
#include "features/wad-download/computation/fileresolve_compute.h"
#include "features/wad-download/computation/iwadallow_compute.h"
#include "features/wad-download/computation/wadstore_compute.h"

//*****************************************************************************
//	CONSOLE VARIABLES

// [rc4l] fua_ per the naming rule: no Zandronum equivalent exists. The names deliberately echo
// Odamex's cl_downloadsites / cl_serverdownload so anyone who has configured a Doom port before
// recognises what they are looking at.
CVAR( Bool, cl_fua_download, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )

// [rc4l] Public WAD mirrors, tried after whatever the server itself advertises. Shipping a default
// list is what makes the feature work out of the box -- an empty default would mean every player has
// to find and type a mirror before their first download, which in practice means nobody downloads
// anything.
//
// The default comes from the repo-root waddownloadsites.txt (compiled in by tools/gen-wadlists.cmake)
// so mirrors can be added and dead ones dropped by pull request, without editing C++. Unlike the IWAD
// allowlist this one IS a CVAR: nothing here is a legal assertion, so a player replacing the list
// entirely costs them nothing but their own downloads.
CVAR( String, cl_fua_downloadsites, zx::kDefaultDownloadSites, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )

// [rc4l] Empty means "the default directory" (DownloadDir below), which is the case that should need
// no configuration.
CVAR( String, cl_fua_download_dir, "", CVAR_ARCHIVE | CVAR_GLOBALCONFIG )

// [rc4l] A ceiling on one file, in megabytes. Not a bandwidth control -- there is no server bandwidth
// to control -- but a bound on what a mirror can make us write to disk. Deliberately well above any
// real resource pack (MM8BDM, the biggest thing commonly hosted, is a few hundred MB): the point is
// to stop a hostile or broken mirror streaming until the disk fills, not to second-guess how large a
// legitimate mod is allowed to be. Held in a 64-bit byte count, so raising it further is just a
// bigger number.
CVAR( Int, cl_fua_download_maxsize, 2048, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )

// [rc4l] Ceiling on the content-addressed store, in MB; 0 means keep everything. Downloads are filed
// under their digest so two servers using the name test.wad no longer overwrite each other, which
// means old versions survive instead of being clobbered -- for someone iterating on a map that is a
// new build every few minutes, so something has to bound it. Least recently used goes first, so what
// survives is what is actually being played.
CVAR( Int, cl_fua_download_maxstore, 4096, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )

namespace
{

//*****************************************************************************
//	SHARED STATE
//
// Written by the worker, read by the main thread. The mutex covers the string members; the counters
// are atomics so a progress read never has to take a lock.

enum class RunState { Idle, Running, Finished };

std::mutex g_mutex;
std::vector<std::string> g_log;			// lines for Tick() to Printf
std::string g_currentFile;				// what is being fetched right now
long long g_received = 0;
long long g_total = -1;					// -1 when the server sent no Content-Length

// Plain ints guarded by g_mutex rather than atomics: every transition is already made under the lock
// alongside a string write, so an atomic would buy nothing and invite the two to disagree.
RunState g_state = RunState::Idle;
bool g_succeeded = false;
bool g_cancel = false;
bool g_completionPending = false;
zx::waddownload::CompleteProc g_onDone = NULL;

// Everything the worker needs, copied out of the engine before it starts. The worker never reads a
// CVAR or an engine global.
struct Job
{
	std::vector<std::string> sites;
	std::vector<zx::waddownload::WantedFile> files;
	std::string dir;
	long long maxBytes;
	long long storeCapBytes;

	Job() : maxBytes(0), storeCapBytes(0) {}
};

// [rc4l] A job waiting for the previous run to stop.
//
// Picking a second server while the first is still downloading used to be a dead end: only one run
// happens at a time, so the new join could not start one and said "you are missing these" -- and then
// the OLD download finished and dragged the player onto the server they had just moved away from.
//
// Switching now abandons the first run and queues the second. It cannot start immediately, because
// the worker only notices the cancel between chunks, so the job waits here and Tick launches it once
// the old run has actually stopped. Deferring beats blocking the main thread on a socket that may be
// stalled.
bool g_haveDeferred = false;
Job g_deferredJob;
zx::waddownload::CompleteProc g_deferredOnDone = NULL;

void Say(const std::string &line)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_log.push_back(line);
}

// [rc4l] Everything that makes a run "in flight", in ONE place.
//
// Two callers start runs -- Start(), and Tick() launching a queued replacement -- and this was
// written out longhand in both. A field added to the run state and reset in only one of them would
// leak from the previous run into the next, which is the sort of bug that shows up as a progress bar
// starting at 40% or a cancel flag that was never cleared.
//
// Caller must already hold g_mutex.
void BeginRunLocked(zx::waddownload::CompleteProc onDone)
{
	g_log.clear();
	g_currentFile.clear();
	g_received = 0;
	g_total = -1;
	g_state = RunState::Running;
	g_succeeded = false;
	g_cancel = false;
	g_completionPending = false;
	g_onDone = onDone;
}

// Hex digests differ only in case between sources; compare them the way they are meant to be equal.
bool EqualsIgnoreCase(const char *a, const std::string &b)
{
	size_t i = 0;
	for (; a[i] != '\0' && i < b.size(); ++i)
	{
		const char ca = (a[i] >= 'A' && a[i] <= 'Z') ? char(a[i] - 'A' + 'a') : a[i];
		const char cb = (b[i] >= 'A' && b[i] <= 'Z') ? char(b[i] - 'A' + 'a') : b[i];
		if (ca != cb)
			return false;
	}
	return a[i] == '\0' && i == b.size();
}

//*****************************************************************************
//	THE WORKER

bool OnProgress(void *, long long received, long long total)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_received = received;
	g_total = total;
	return !g_cancel;
}

bool Cancelled()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_cancel;
}

// [rc4l] How long to wait out a server that has the file but no free download slot, and how many
// times. Four minutes total: long enough to outlast the transfer ahead of us at the default slot
// count, short enough that a player is not left staring at a frozen join if the server never frees
// one. After that the other mirrors are tried, which is the right order -- the server is the source
// most likely to have the file, not the only one allowed to.
const int kMaxBusyWaits = 8;
const int kBusyWaitMs = 30000;

// Sleep in slices so a cancel is noticed in a third of a second rather than half a minute. Returns
// false if the run was cancelled while waiting.
bool SleepUnlessCancelled(int totalMs)
{
	const int sliceMs = 250;
	for (int waited = 0; waited < totalMs; waited += sliceMs)
	{
		if (Cancelled())
			return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(sliceMs));
	}
	return !Cancelled();
}

//*****************************************************************************
//	THE CONTENT-ADDRESSED STORE
//
// See computation/wadstore_compute.h for why files are filed under their digest. Everything here is
// confined to OUR download folder: a player's own WADs are read and never moved, renamed or deleted.

// 64-bit stat, because a resource pack can exceed what the 32-bit one reports.
#ifdef _WIN32
  typedef struct _stat64 zx_stat_t;
  #define zx_stat _stat64
#else
  typedef struct stat zx_stat_t;
  #define zx_stat stat
#endif

bool StatFile(const std::string &path, long long &outSize, long long &outMtime)
{
	zx_stat_t info;
	if (zx_stat(path.c_str(), &info) != 0)
		return false;

	outSize = static_cast<long long>(info.st_size);
	outMtime = static_cast<long long>(info.st_mtime);
	return true;
}

bool FileExists(const std::string &path)
{
	long long size = 0;
	long long mtime = 0;
	return StatFile(path, size, mtime);
}

// Move the file currently under `name` into the store, keyed by ITS digest -- unless it is already
// the content we are about to write, in which case there is nothing worth keeping.
void ArchiveExistingCopy(const std::string &dir, const std::string &name,
	const std::string &incomingMd5)
{
	const std::string flatPath = dir + name;
	if (!FileExists(flatPath))
		return;

	char existing[33];
	if (!zx::Md5OfFile(flatPath.c_str(), existing, sizeof existing))
		return;								// unreadable: leave it alone rather than lose it blindly

	if (!incomingMd5.empty() && EqualsIgnoreCase(existing, incomingMd5))
		return;								// same bytes; archiving would just duplicate them

	const std::string relative = zx::StoredRelativePath(existing, name, 32);
	if (relative.empty())
		return;

	const std::string target = dir + relative;
	if (FileExists(target))
	{
		// Already archived from an earlier round. The flat copy is redundant, and std::rename would
		// refuse to replace it on Windows anyway.
		std::remove(flatPath.c_str());
		return;
	}

	CreatePath((dir + zx::StoredRelativeDir(existing, 32)).c_str());
	std::rename(flatPath.c_str(), target.c_str());
}

// Bring the store within its cap, least recently used first. Only ever walks by-hash/.
void PruneStore(const std::string &dir, long long capBytes)
{
	if (capBytes <= 0)
		return;

	const std::string storeRoot = dir + zx::kStoreDirName;

	std::vector<zx::StoreEntry> entries;
	std::vector<std::string> paths;
	std::vector<std::string> dirs;

	findstate_t digestDir;
	void *digestHandle = I_FindFirst((storeRoot + "/*").c_str(), &digestDir);
	if (digestHandle == ((void *)-1))
		return;

	do
	{
		const char *digestName = I_FindName(&digestDir);
		if ((I_FindAttr(&digestDir) & FA_DIREC) == 0)
			continue;
		if ((std::strcmp(digestName, ".") == 0) || (std::strcmp(digestName, "..") == 0))
			continue;

		const std::string oneDir = storeRoot + "/" + digestName;

		findstate_t fileEntry;
		void *fileHandle = I_FindFirst((oneDir + "/*").c_str(), &fileEntry);
		if (fileHandle == ((void *)-1))
			continue;

		do
		{
			if (I_FindAttr(&fileEntry) & FA_DIREC)
				continue;

			const std::string path = oneDir + "/" + I_FindName(&fileEntry);
			long long size = 0;
			long long mtime = 0;
			if (!StatFile(path, size, mtime))
				continue;

			entries.push_back(zx::StoreEntry(size, mtime));
			paths.push_back(path);
			dirs.push_back(oneDir);
		} while (I_FindNext(fileHandle, &fileEntry) == 0);
		I_FindClose(fileHandle);
	} while (I_FindNext(digestHandle, &digestDir) == 0);
	I_FindClose(digestHandle);

	const std::vector<size_t> doomed = zx::ComputePruneOrder(entries, capBytes);
	for (size_t i = 0; i < doomed.size(); ++i)
	{
		std::remove(paths[doomed[i]].c_str());
		zx_rmdir(dirs[doomed[i]].c_str());	// only succeeds once the directory is empty
	}
}

// [rc4l] Delete .part files left by a run that died with the process.
//
// A transfer writes to <name>.part and renames on success, and cancelling deletes it -- but a crash
// or a plain quit mid-download has no way out to hook, so the partial file simply stays. Nothing ever
// removed them: the store prune only walks by-hash/, so they accumulated in the download folder for
// the life of the install.
//
// Swept when a run starts rather than on shutdown, because that is the moment we know nothing is
// using them: only one run happens at a time, so anything ending in .part here belongs to a dead one.
// Names are collected before anything is deleted -- removing entries while enumerating a directory is
// not something to rely on across platforms.
void SweepStalePartFiles(const std::string &dir)
{
	findstate_t entry;
	void *handle = I_FindFirst((dir + "*.part").c_str(), &entry);
	if (handle == ((void *)-1))
		return;

	std::vector<std::string> doomed;
	do
	{
		if (I_FindAttr(&entry) & FA_DIREC)
			continue;
		doomed.push_back(dir + I_FindName(&entry));
	} while (I_FindNext(handle, &entry) == 0);
	I_FindClose(handle);

	for (size_t i = 0; i < doomed.size(); ++i)
		std::remove(doomed[i].c_str());
}

// The first bytes of `path`, for the content gate. Short reads are fine -- a file too small to have a
// header is not an IWAD, and ClassifyDownloadedFile says so.
size_t ReadHeader(const std::string &path, char *out, size_t want)
{
	FILE *fp = std::fopen(path.c_str(), "rb");
	if (fp == NULL)
		return 0;
	const size_t got = std::fread(out, 1, want, fp);
	std::fclose(fp);
	return got;
}

// Fetch one file. Returns true only when it is on disk under its final name and has passed the
// content gate.
bool FetchOne(const Job &job, const zx::waddownload::WantedFile &wanted)
{
	const zx::DownloadVerdict pre = zx::ClassifyWantedFile(wanted.name, wanted.isIwad);
	if (pre != zx::DownloadVerdict::Allowed)
	{
		Say(std::string("Won't download ") + wanted.name + ": " + zx::DownloadVerdictReason(pre));
		return false;
	}

	const std::vector<std::string> urls = zx::BuildCandidateUrls(job.sites, wanted.name);
	if (urls.empty())
	{
		Say(std::string("No download sites configured for ") + wanted.name + ".");
		return false;
	}

	// Written under a .part name and renamed only on success, so a half-finished file is never
	// visible to the WAD search under a name the engine would try to load.
	const std::string finalPath = job.dir + wanted.name;
	const std::string partPath = finalPath + ".part";

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_currentFile = wanted.name;
		g_received = 0;
		g_total = -1;
	}

	// [rc4l] Why verification lives INSIDE the mirror loop: a site that serves the wrong bytes under
	// the right name is a bad mirror, not a bad join. Failing outright there would let one stale or
	// mislabelled copy deny a file that the next site has correctly. So a rejected file falls through
	// to the next candidate, and only an exhausted list is an error -- reported with the last
	// rejection rather than a bare "not found", because "no site had it" and "every site had the
	// wrong thing" need completely different responses from whoever reads it.
	std::string rejectReason;

	// The digest of whatever actually landed, which is what the file gets filed under.
	std::string contentMd5;

	bool got = false;
	int busyWaits = 0;
	for (size_t i = 0; i < urls.size() && !got; ++i)
	{
		// [rc4l] 503 is the one failure worth waiting through rather than walking past. It means this
		// host HAS the file and is out of download slots -- which for a server serving its own WADs
		// is the ordinary state during a map change, when everyone joins at once. Moving on there
		// would abandon the only source certain to have a file that may exist nowhere else. So the
		// SAME url is retried a few times before the rest of the list is considered.
		// [rc4l] Say where this is coming from, before it starts rather than after it finishes.
		//
		// A download that stalls is the case that needs this: without it the console shows a file
		// name and a progress bar that stopped, and no way to tell whether the culprit is a mirror
		// worth skipping or the player's own connection. Host only, never the whole URL -- see
		// DownloadSourceName for why a signed mirror URL must not reach a log.
		Say(std::string("Downloading ") + wanted.name + " from "
			+ zx::DownloadSourceName(urls[i]) + " (source " + std::to_string(i + 1)
			+ " of " + std::to_string(urls.size()) + ")");

		// [rc4l] Fresh counters per SOURCE, not just per file: StatusLine reads these to tell "a
		// source is delivering" (percent) apart from "still knocking on doors" (searching...), and
		// a dead mirror leaving the previous attempt's figures behind would show a progress bar for
		// a connection that does not exist.
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_received = 0;
			g_total = -1;
		}

		zx::HttpFileResult r = zx::HttpFileResult::NetworkError;
		for (;;)
		{
			r = zx::HttpGetToFile(urls[i].c_str(), partPath.c_str(), job.maxBytes, OnProgress, NULL);
			if (r != zx::HttpFileResult::Busy)
				break;

			if (busyWaits >= kMaxBusyWaits)
			{
				Say("Gave up waiting for a download slot; trying other sites.");
				break;					// falls into the switch's default: next mirror
			}

			busyWaits++;
			Say(std::string("Every download slot on that server is busy; waiting (attempt ")
				+ std::to_string(busyWaits) + " of " + std::to_string(kMaxBusyWaits) + ").");
			if (!SleepUnlessCancelled(kBusyWaitMs))
				return false;
		}

		switch (r)
		{
		case zx::HttpFileResult::Ok:
			break;					// verified below; `got` is only set once it passes

		case zx::HttpFileResult::NotFound:
			// Silent: with three spellings across half a dozen mirrors, "not here" is the ordinary
			// case and reporting each one would bury the result in noise.
			break;

		case zx::HttpFileResult::Cancelled:
			Say("Download cancelled.");
			return false;

		case zx::HttpFileResult::TooLarge:
			Say(std::string("Refused ") + wanted.name + ": larger than cl_fua_download_maxsize.");
			return false;

		case zx::HttpFileResult::WriteFailed:
			Say(std::string("Can't write to ") + job.dir + " -- check cl_fua_download_dir.");
			return false;

		default:
			break;					// HttpError / NetworkError: try the next mirror
		}

		if (Cancelled())
			return false;
		if (r != zx::HttpFileResult::Ok)
			continue;

		char header[8];
		const size_t headerLen = ReadHeader(partPath, header, sizeof header);

		// SHA-256 only when this is going to be loaded as a game -- which is the IWAD slot OR IWAD
		// magic, not magic alone: Chex Quest ships PWAD magic and is still an IWAD. Hashing every mod
		// would cost a full extra read of a file we have no list to check it against.
		std::string sha;
		if (wanted.isIwad || zx::HeaderIsIwadMagic(header, headerLen))
		{
			char hex[65];
			if (zx::Sha256OfFile(partPath.c_str(), hex, sizeof hex))
				sha = hex;			// on failure `sha` stays empty, which the gate treats as "cannot vouch"
		}

		// The gate: a commercial game arriving under a name nobody would question dies here.
		const zx::DownloadVerdict post =
			zx::ClassifyDownloadedFile(wanted.name, wanted.isIwad, header, headerLen, sha);
		if (post != zx::DownloadVerdict::Allowed)
		{
			std::remove(partPath.c_str());
			rejectReason = zx::DownloadVerdictReason(post);
			continue;
		}

		// One hash, two jobs. Integrity, separately from legality: does this match what the SERVER
		// says it runs? Only possible when the server sent hashes; an empty expectation means
		// "cannot check", and we do not pretend otherwise. And it is what the file gets filed under
		// below, so it is computed either way rather than only when there is something to compare to.
		{
			char md5[33];
			if (zx::Md5OfFile(partPath.c_str(), md5, sizeof md5))
				contentMd5 = md5;

			if (!wanted.expectedMd5.empty() &&
				(contentMd5.empty() || !EqualsIgnoreCase(contentMd5.c_str(), wanted.expectedMd5)))
			{
				std::remove(partPath.c_str());
				rejectReason = "this mirror's copy is not the one the server is running";
				contentMd5.clear();
				continue;
			}
		}

		got = true;
	}

	if (!got)
	{
		if (rejectReason.empty())
			Say(std::string("Couldn't find ") + wanted.name + " on any download site.");
		else
			Say(std::string("Discarded every copy of ") + wanted.name + " we found: " + rejectReason);
		return false;
	}

	// [rc4l] Move whatever was already sitting under this name into the content-addressed store
	// before overwriting it. test.wad is a common name, and the copy about to be replaced belongs to
	// some other server that will be joined again -- clobbering it means re-downloading it then, and
	// re-downloading this one when they switch back, forever.
	ArchiveExistingCopy(job.dir, wanted.name, contentMd5);

	// std::rename will not replace an existing file on Windows, and a stale same-named file is
	// exactly what we want to overwrite -- we just proved this one is complete and permitted.
	std::remove(finalPath.c_str());
	if (std::rename(partPath.c_str(), finalPath.c_str()) != 0)
	{
		std::remove(partPath.c_str());
		Say(std::string("Couldn't save ") + wanted.name + " to " + job.dir + ".");
		return false;
	}

	// Versions accumulate instead of overwriting, which is the point and also the cost. For someone
	// iterating on a map this is a new build every few minutes, so the archive has to be bounded.
	PruneStore(job.dir, job.storeCapBytes);

	Say(std::string("Downloaded ") + wanted.name + ".");
	return true;
}

void RunJob(Job job)
{
	bool ok = true;
	for (size_t i = 0; i < job.files.size() && ok; ++i)
	{
		if (Cancelled())
		{
			ok = false;
			break;
		}
		ok = FetchOne(job, job.files[i]);
	}

	std::lock_guard<std::mutex> lock(g_mutex);
	g_state = RunState::Finished;
	g_succeeded = ok && !g_cancel;
	g_completionPending = true;
	g_currentFile.clear();
}

//*****************************************************************************
//	MAIN-THREAD HELPERS

// [rc4l] Make the download directory findable by BaseFileSearch, which walks the config's
// FileSearch.Directories. Without this a downloaded file would be invisible to the very join that
// fetched it -- and adding the path once, in the config, means it also stays findable next session
// and for files the player drops in there by hand.
void RegisterDownloadDirInSearchPath(const char *dir)
{
	if (GameConfig == NULL || dir == NULL || dir[0] == '\0')
		return;
	if (!GameConfig->SetSection("FileSearch.Directories", true))
		return;

	const char *key, *value;
	while (GameConfig->NextInSection(key, value))
	{
		if (stricmp(key, "Path") == 0 && stricmp(value, dir) == 0)
			return;					// already there; do not grow the section every launch
	}
	GameConfig->SetValueForKey("Path", dir, true);
}

} // namespace

namespace zx { namespace waddownload {

FString DownloadDir()
{
	FString dir = *cl_fua_download_dir;
	if (dir.IsEmpty())
	{
		// A per-user, writable location that already exists on every platform -- and one that falls
		// back to progdir for a portable install, which is where a portable install expects its files.
		dir = M_GetSavegamesPath();
		if (dir.IsEmpty())
			dir = progdir;
		if (dir.Len() > 0 && dir[dir.Len() - 1] != '/' && dir[dir.Len() - 1] != '\\')
			dir += "/";
		dir += "Downloads/";
	}
	else if (dir.Len() > 0 && dir[dir.Len() - 1] != '/' && dir[dir.Len() - 1] != '\\')
	{
		dir += "/";
	}

	FixPathSeperator(dir);
	return dir;
}

// Walk a resolve plan and return the first step that holds the wanted bytes. A Stat step is taken on
// the strength of its path, since the digest names the folder. Only a Hash step is read.
static FString WalkResolvePlan(const std::vector<zx::ResolveStep> &steps, const std::string &digest)
{
	for (size_t i = 0; i < steps.size(); ++i)
	{
		if (!FileExists(steps[i].path))
			continue;

		if (steps[i].check == zx::ResolveCheck::Stat)
			return FString(steps[i].path.c_str());

		char actual[33];
		if (zx::Md5OfFile(steps[i].path.c_str(), actual, sizeof actual) &&
			EqualsIgnoreCase(actual, digest))
		{
			return FString(steps[i].path.c_str());
		}
	}

	return FString();
}

FString FindLocalCopy(const char *name, const char *md5Hex)
{
	if ((name == NULL) || (md5Hex == NULL) || (md5Hex[0] == '\0'))
		return FString();

	// No search hits: our own folder is the whole of this question by contract.
	return WalkResolvePlan(zx::PlanFileResolve(name, md5Hex, DownloadDir().GetChars(),
		std::vector<std::string>()), md5Hex);
}

FString FindVerifiedCopy(const char *name, const char *md5Hex)
{
	if ((name == NULL) || (md5Hex == NULL) || (md5Hex[0] == '\0'))
		return FString();

	TArray<FString> found;
	zx::FindAllFilesInEngineSearchPaths(name, found);

	std::vector<std::string> hits;
	hits.reserve(found.Size());
	for (unsigned i = 0; i < found.Size(); ++i)
		hits.push_back(found[i].GetChars());

	return WalkResolvePlan(zx::PlanFileResolve(name, md5Hex, DownloadDir().GetChars(), hits), md5Hex);
}

bool IsRunning()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_state == RunState::Running;
}

bool IsAvailable()
{
	return cl_fua_download && !IsRunning();
}

void Cancel()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_state == RunState::Running)
		g_cancel = true;

	// A queued replacement is cancelled too -- otherwise "stop this download" would start the next
	// one a moment later, which is the opposite of what was asked.
	g_haveDeferred = false;
	g_deferredOnDone = NULL;
}

void Abandon()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_state != RunState::Running)
		return;

	g_cancel = true;

	// Drop the completion. This is the difference from Cancel: the run being abandoned belongs to a
	// join the player has moved on from, so resuming it would pull them onto a server they left.
	g_onDone = NULL;
}

bool Start(const std::vector<std::string> &extraSites,
	const std::vector<std::string> &lastResortSites, const std::vector<WantedFile> &files,
	CompleteProc onDone)
{
	if (files.empty())
		return false;

	if (!cl_fua_download)
	{
		Printf(TEXTCOLOR_ORANGE "Downloading is off (cl_fua_download 0).\n");
		return false;
	}
	// [rc4l] A run already going means the player picked another server. Abandon that one and queue
	// this -- see g_deferredJob for why it is queued rather than started here.
	const bool replacing = IsRunning();
	if (replacing)
		Abandon();

	Job job;

	// The server's own advertised site first, the public mirrors next, and the last-resort sources
	// (the hosting machine's own endpoint) at the end -- AssembleSiteOrder is the policy, with the
	// reasoning and the regression test beside it.
	job.sites = zx::AssembleSiteOrder(extraSites,
		zx::SplitOnWhitespace(std::string(*cl_fua_downloadsites)), lastResortSites);
	if (zx::NormalizeDownloadSites(job.sites).empty())
	{
		Printf(TEXTCOLOR_ORANGE "No usable download sites. Set cl_fua_downloadsites.\n");
		return false;
	}

	// Refusals are reported here, on the main thread, before anything is started -- a player told
	// "that's a commercial IWAD" should hear it immediately, not after a mirror sweep.
	std::vector<WantedFile> permitted;
	for (size_t i = 0; i < files.size(); ++i)
	{
		const DownloadVerdict v = ClassifyWantedFile(files[i].name, files[i].isIwad);
		if (v == DownloadVerdict::Allowed)
			permitted.push_back(files[i]);
		else
			Printf(TEXTCOLOR_ORANGE "Won't download %s: %s\n", files[i].name.c_str(),
				DownloadVerdictReason(v));
	}
	if (permitted.empty())
		return false;
	job.files = permitted;

	const FString dir = DownloadDir();
	CreatePath(dir);
	if (!DirEntryExists(dir))
	{
		Printf(TEXTCOLOR_ORANGE "Can't create the download folder %s.\n", dir.GetChars());
		return false;
	}
	RegisterDownloadDirInSearchPath(dir);
	job.dir = dir.GetChars();

	// Clear out anything a previous run left behind when it died with the process.
	SweepStalePartFiles(job.dir);

	const int capMB = *cl_fua_download_maxsize > 0 ? *cl_fua_download_maxsize : 1;
	job.maxBytes = (long long)capMB * 1024LL * 1024LL;

	// Snapshotted with everything else: the worker never reads a CVAR.
	const int storeMB = *cl_fua_download_maxstore;
	job.storeCapBytes = (storeMB > 0) ? ((long long)storeMB * 1024LL * 1024LL) : 0;

	// Replacing a run in flight: the worker only notices the cancel between chunks, so this waits for
	// it rather than blocking the main thread on a socket that may be stalled. Tick starts it.
	if (replacing)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_haveDeferred = true;
		g_deferredJob = job;
		g_deferredOnDone = onDone;
		Printf(TEXTCOLOR_ORANGE "Stopped the previous download; starting this one instead.\n");
		return true;
	}

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		BeginRunLocked(onDone);
	}

	Printf("Downloading %u file%s to %s\n", (unsigned)job.files.size(),
		job.files.size() == 1 ? "" : "s", dir.GetChars());

	std::thread(RunJob, job).detach();
	return true;
}

FString StatusLine()
{
	std::string file;
	long long received, total;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_state != RunState::Running || g_currentFile.empty())
			return FString();
		file = g_currentFile;
		received = g_received;
		total = g_total;
	}

	// [rc4l] The wording and the width rules live in FormatDownloadStatus, where every shape of the
	// line -- searching, percent, length-unknown -- is pinned by tests instead of read off a screen.
	return FString(zx::FormatDownloadStatus(file, received, total).c_str());
}

void Tick()
{
	std::vector<std::string> lines;
	CompleteProc done = NULL;
	bool succeeded = false;
	bool startDeferred = false;
	Job deferred;
	CompleteProc deferredDone = NULL;

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_state == RunState::Idle && g_log.empty() && !g_haveDeferred)
			return;

		lines.swap(g_log);

		if (g_completionPending)
		{
			g_completionPending = false;
			g_state = RunState::Idle;
			done = g_onDone;
			succeeded = g_succeeded;
			g_onDone = NULL;
		}

		// [rc4l] The abandoned run has stopped, so the job the player actually wants can go. Started
		// from here rather than from Start() because only the worker knows when it has let go.
		if (g_haveDeferred && (g_state == RunState::Idle))
		{
			startDeferred = true;
			deferred = g_deferredJob;
			deferredDone = g_deferredOnDone;

			g_haveDeferred = false;
			g_deferredJob = Job();
			g_deferredOnDone = NULL;

			BeginRunLocked(deferredDone);
		}
	}

	// Printf outside the lock: it can re-enter a lot of engine machinery, and holding the worker's
	// mutex across that is how a console command that queries progress would deadlock.
	for (size_t i = 0; i < lines.size(); ++i)
		Printf("%s\n", lines[i].c_str());

	if (startDeferred)
	{
		Printf("Downloading %u file%s to %s\n", (unsigned)deferred.files.size(),
			deferred.files.size() == 1 ? "" : "s", deferred.dir.c_str());
		std::thread(RunJob, deferred).detach();
	}

	if (done != NULL)
		done(succeeded);
}

}} // namespace zx::waddownload

//*****************************************************************************
//	CONSOLE COMMANDS

// [rc4l] fua_ per the naming rule. Mirrors Odamex's `download get <file>` -- useful on its own, and
// the quickest way to check a mirror list without needing a server that wants the file.
CCMD( fua_download )
{
	if ( argv.argc( ) < 2 )
	{
		Printf( "Usage: fua_download <filename>\n"
			"Downloads a PWAD from the sites in cl_fua_downloadsites.\n" );
		return;
	}

	std::vector<zx::waddownload::WantedFile> files;
	files.push_back( zx::waddownload::WantedFile( argv[1], false ));
	zx::waddownload::Start( std::vector<std::string>( ), std::vector<std::string>( ), files, NULL );
}

CCMD( fua_download_stop )
{
	if ( zx::waddownload::IsRunning( ))
		zx::waddownload::Cancel( );
	else
		Printf( "No download in progress.\n" );
}

CCMD( fua_download_status )
{
	const FString line = zx::waddownload::StatusLine( );
	if ( line.IsNotEmpty( ))
		Printf( "%s\n", line.GetChars( ));
	else
		Printf( "No download in progress. Files go to %s\n",
			zx::waddownload::DownloadDir( ).GetChars( ));
}
