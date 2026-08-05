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

#include <chrono>
#include <cstdio>
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
#include "m_misc.h"
#include "v_text.h"

// [rc4l] kDefaultDownloadSites, generated from the repo-root waddownloadsites.txt.
#include "zx_waddownload_lists.h"

#include "features/net/zx_httpfile.h"
#include "features/wad-download/zx_filehash.h"
#include "features/wad-download/zx_waddownload.h"
#include "features/wad-download/computation/downloadplan_compute.h"
#include "features/wad-download/computation/iwadallow_compute.h"

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
};

void Say(const std::string &line)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_log.push_back(line);
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

std::string HumanBytes(long long n)
{
	char buf[64];
	if (n < 0)
		return "?";
	if (n >= 1024LL * 1024LL)
		std::snprintf(buf, sizeof buf, "%.1f MB", double(n) / (1024.0 * 1024.0));
	else
		std::snprintf(buf, sizeof buf, "%.0f KB", double(n) / 1024.0);
	return buf;
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

	bool got = false;
	int busyWaits = 0;
	for (size_t i = 0; i < urls.size() && !got; ++i)
	{
		// [rc4l] 503 is the one failure worth waiting through rather than walking past. It means this
		// host HAS the file and is out of download slots -- which for a server serving its own WADs
		// is the ordinary state during a map change, when everyone joins at once. Moving on there
		// would abandon the only source certain to have a file that may exist nowhere else. So the
		// SAME url is retried a few times before the rest of the list is considered.
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

		// Integrity, separately from legality: does this match what the SERVER says it runs? Only
		// possible when the server sent hashes; an empty expectation means "cannot check", and we do
		// not pretend otherwise.
		if (!wanted.expectedMd5.empty())
		{
			char md5[33];
			if (!zx::Md5OfFile(partPath.c_str(), md5, sizeof md5) ||
				!EqualsIgnoreCase(md5, wanted.expectedMd5))
			{
				std::remove(partPath.c_str());
				rejectReason = "this mirror's copy is not the one the server is running";
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

	// std::rename will not replace an existing file on Windows, and a stale same-named file is
	// exactly what we want to overwrite -- we just proved this one is complete and permitted.
	std::remove(finalPath.c_str());
	if (std::rename(partPath.c_str(), finalPath.c_str()) != 0)
	{
		std::remove(partPath.c_str());
		Say(std::string("Couldn't save ") + wanted.name + " to " + job.dir + ".");
		return false;
	}

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
}

bool Start(const std::vector<std::string> &extraSites, const std::vector<WantedFile> &files,
	CompleteProc onDone)
{
	if (files.empty())
		return false;

	if (!cl_fua_download)
	{
		Printf(TEXTCOLOR_ORANGE "Downloading is off (cl_fua_download 0).\n");
		return false;
	}
	if (IsRunning())
	{
		Printf(TEXTCOLOR_ORANGE "A download is already in progress.\n");
		return false;
	}

	Job job;

	// The server's own advertised site first: it is the one that actually has this server's files.
	// Everything after it is a general-purpose mirror that may or may not.
	job.sites = extraSites;
	const std::vector<std::string> configured =
		zx::SplitOnWhitespace(std::string(*cl_fua_downloadsites));
	job.sites.insert(job.sites.end(), configured.begin(), configured.end());
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

	const int capMB = *cl_fua_download_maxsize > 0 ? *cl_fua_download_maxsize : 1;
	job.maxBytes = (long long)capMB * 1024LL * 1024LL;

	{
		std::lock_guard<std::mutex> lock(g_mutex);
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

	FString out;
	if (total > 0)
	{
		out.Format("%s  %d%%  (%s of %s)", file.c_str(), int((received * 100) / total),
			HumanBytes(received).c_str(), HumanBytes(total).c_str());
	}
	else
	{
		// No Content-Length: a percentage would be a guess, so show what has actually arrived.
		out.Format("%s  %s", file.c_str(), HumanBytes(received).c_str());
	}
	return out;
}

void Tick()
{
	std::vector<std::string> lines;
	CompleteProc done = NULL;
	bool succeeded = false;

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_state == RunState::Idle && g_log.empty())
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
	}

	// Printf outside the lock: it can re-enter a lot of engine machinery, and holding the worker's
	// mutex across that is how a console command that queries progress would deadlock.
	for (size_t i = 0; i < lines.size(); ++i)
		Printf("%s\n", lines[i].c_str());

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
	zx::waddownload::Start( std::vector<std::string>( ), files, NULL );
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
