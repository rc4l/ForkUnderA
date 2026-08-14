// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_wadlibrary.h for the design and the threading contract it inherits.

#include "features/wad-library/zx_wadlibrary.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>

#include "cmdlib.h"
#include "c_dispatch.h"
#include "doomtype.h"
#include "gameconfigfile.h"
#include "i_system.h"		// findstate_t / I_Find* -- the same walk the download store prunes with
#include "m_misc.h"
#include "v_text.h"

#include "features/wad-download/zx_filehash.h"
#include "features/wad-download/zx_waddownload.h"

namespace zx { namespace wadlibrary {

namespace
{

// ---------------------------------------------------------------- shared with the worker

std::mutex g_mutex;

// Everything below is written by the worker under the lock and read by Tick(). Plain std::string
// and PODs only: an FString off the main thread is one of the things the contract forbids.
std::vector<LibraryFile> g_pending;
bool g_workerDone = false;
bool g_workerHitCap = false;

// Read by both sides without the lock, which is why it is atomic. The worker checks it between
// directories, so cancelling is prompt without being able to interrupt a listing mid-way.
std::atomic<bool> g_cancel(false);
std::atomic<bool> g_running(false);

// ---------------------------------------------------------------- main thread only

std::vector<LibraryFile> g_files;
ScanState g_state = ScanState::Idle;
bool g_hitCap = false;

// [rc4l] Whether a scan has been ASKED FOR, which is not the same as one being under way or having
// finished, and the difference is a bug that spins a CPU core forever.
//
// Begin() is called from the draw, before Tick() -- so in the frame after the worker finishes there
// is a moment when it is no longer running and the state has not yet been moved to Done. Keyed on
// the state alone, Begin() reads that as "nothing has happened yet" and starts another scan, which
// finishes, and is started again the next frame. The screen sits on "looking..." forever while the
// disk is walked over and over.
bool g_started = false;

// path -> md5, for files we have hashed this session. See HashOf.
std::vector<std::pair<std::string, std::string> > g_hashes;

// ---------------------------------------------------------------- the walk

bool StatFile(const std::string &path, long long &size, long long &mtime)
{
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
		return false;

	size = static_cast<long long>(st.st_size);
	mtime = static_cast<long long>(st.st_mtime);
	return true;
}

// The last component of a directory path, which is what the list shows to tell copies apart.
std::string LeafOf(const std::string &dir)
{
	size_t cut = dir.find_last_of("/\\");
	if (cut == std::string::npos)
		return dir;

	// A trailing separator would give an empty leaf, so step back over it first.
	if (cut + 1 == dir.size())
	{
		if (cut == 0)
			return std::string();
		cut = dir.find_last_of("/\\", cut - 1);
		if (cut == std::string::npos)
			return dir.substr(0, dir.size() - 1);
	}

	return dir.substr(cut + 1);
}

// One directory, and its children down to the depth cap. Recursion is bounded twice over: by depth,
// and by the running total, because a search path can name a drive root.
void WalkDirectory(const std::string &dir, int depth, std::vector<LibraryFile> &out, bool &hitCap)
{
	if (hitCap || g_cancel.load())
		return;
	if (depth > LibraryDepthCap())
		return;

	findstate_t entry;
	void *handle = I_FindFirst((dir + "/*").c_str(), &entry);
	if (handle == ((void *)-1))
		return;

	std::vector<std::string> subdirs;
	const std::string leaf = LeafOf(dir);

	do
	{
		const char *found = I_FindName(&entry);
		if ((found == NULL) || (found[0] == '\0'))
			continue;
		if ((std::string(found) == ".") || (std::string(found) == ".."))
			continue;

		const std::string path = dir + "/" + found;

		if (I_FindAttr(&entry) & FA_DIREC)
		{
			subdirs.push_back(path);
			continue;
		}

		const std::string name = found;

		// [rc4l] The three refusals, cheapest first. Extension is a string compare on what we
		// already have; the other two are lists. None of them opens anything.
		if (!IsLoadableWadName(name))
			continue;
		if (IsEngineOwnedName(name) || IsKnownIwadName(name))
			continue;

		if (out.size() >= LibraryFileCap())
		{
			hitCap = true;
			break;
		}

		LibraryFile file;
		file.path = path;
		file.name = name;
		file.folder = leaf;
		file.key = SearchFold(name);

		// [rc4l] One stat per accepted file, and only per ACCEPTED file -- the filters above run
		// first precisely so a folder of screenshots costs nothing. Windows already has the size in
		// the find data, but there is no portable accessor for it, and this is the cheap side of the
		// scan either way.
		if (!StatFile(path, file.size, file.mtime))
			continue;

		out.push_back(file);
	} while (I_FindNext(handle, &entry) == 0);

	I_FindClose(handle);

	for (size_t i = 0; i < subdirs.size(); ++i)
		WalkDirectory(subdirs[i], depth + 1, out, hitCap);
}

void RunScan(std::vector<std::string> roots)
{
	std::vector<LibraryFile> found;
	bool hitCap = false;

	for (size_t i = 0; i < roots.size(); ++i)
	{
		if (g_cancel.load())
			break;

		WalkDirectory(roots[i], 0, found, hitCap);
	}

	// [rc4l] Sorted HERE, once, on the thread that has time for it. Rows come out of
	// BuildLibraryRows in this order, and it skips its own sort when the input already has it -- so
	// the cost of ordering twenty thousand files is paid on a worker at the end of a scan instead
	// of on the main thread after every keystroke.
	std::sort(found.begin(), found.end(), LibraryFileLess);

	std::lock_guard<std::mutex> lock(g_mutex);
	g_pending.swap(found);
	g_workerHitCap = hitCap;
	g_workerDone = true;
	g_running.store(false);
}

// Every folder worth walking, deduplicated, on the MAIN thread -- it reads GameConfig, which the
// worker may not touch.
std::vector<std::string> CollectRoots()
{
	std::vector<std::string> roots;

	const FString ours = zx::waddownload::DownloadDir();
	if (ours.IsNotEmpty())
	{
		FString trimmed = ours;
		while ((trimmed.Len() > 0) &&
			((trimmed[trimmed.Len() - 1] == '/') || (trimmed[trimmed.Len() - 1] == '\\')))
		{
			trimmed.Truncate(trimmed.Len() - 1);
		}
		if (trimmed.IsNotEmpty())
			roots.push_back(std::string(trimmed.GetChars()));
	}

	if (GameConfig != NULL && GameConfig->SetSection("FileSearch.Directories"))
	{
		const char *key, *value;
		while (GameConfig->NextInSection(key, value))
		{
			if (stricmp(key, "Path") != 0)
				continue;

			FString dir = NicePath(value);
			if (dir.IsEmpty())
				continue;
			FixPathSeperator(dir);

			while ((dir.Len() > 0) &&
				((dir[dir.Len() - 1] == '/') || (dir[dir.Len() - 1] == '\\')))
			{
				dir.Truncate(dir.Len() - 1);
			}

			if (dir.IsEmpty() || !DirEntryExists(dir.GetChars()))
				continue;

			roots.push_back(std::string(dir.GetChars()));
		}
	}

	// [rc4l] Deduplicated, because the same folder legitimately appears twice: our download folder
	// is registered in FileSearch.Directories by design. Walking it twice would list every file in
	// it twice, and the row deduplication downstream would then hide half the collection behind a
	// "2 copies" mark that is really one file counted twice.
	std::vector<std::string> unique;
	for (size_t i = 0; i < roots.size(); ++i)
	{
		bool seen = false;
		for (size_t j = 0; j < unique.size(); ++j)
		{
#ifdef _WIN32
			if (stricmp(unique[j].c_str(), roots[i].c_str()) == 0)
#else
			if (unique[j] == roots[i])
#endif
			{
				seen = true;
				break;
			}
		}

		if (!seen)
			unique.push_back(roots[i]);
	}

	return unique;
}

} // namespace

void Begin(bool force)
{
	if (g_running.load())
		return;
	if (!force && g_started)
		return;

	const std::vector<std::string> roots = CollectRoots();
	if (roots.empty())
	{
		g_started = true;
		g_state = ScanState::Failed;
		return;
	}

	g_started = true;

	g_files.clear();
	g_hitCap = false;
	g_state = ScanState::Running;

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_pending.clear();
		g_workerDone = false;
		g_workerHitCap = false;
	}

	g_cancel.store(false);
	g_running.store(true);

	std::thread(RunScan, roots).detach();
}

void Cancel()
{
	g_cancel.store(true);
}

ScanState State()
{
	return g_state;
}

size_t Found()
{
	return g_files.size();
}

bool HitCap()
{
	return g_hitCap;
}

void Tick()
{
	if (g_state != ScanState::Running)
		return;

	std::vector<LibraryFile> drained;
	bool done = false;

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_workerDone)
			return;

		drained.swap(g_pending);
		done = true;
		g_hitCap = g_workerHitCap;
	}

	if (!done)
		return;

	g_files.swap(drained);
	g_state = ScanState::Done;
}

const std::vector<LibraryFile> &Files()
{
	return g_files;
}

std::string HashOf(const LibraryFile &file)
{
	for (size_t i = 0; i < g_hashes.size(); ++i)
	{
		if (g_hashes[i].first == file.path)
			return g_hashes[i].second;
	}

	char hex[33];
	if (!zx::Md5OfFile(file.path.c_str(), hex, sizeof hex))
		return std::string();

	const std::string digest(hex);
	g_hashes.push_back(std::make_pair(file.path, digest));
	return digest;
}

}} // namespace zx::wadlibrary

// [rc4l] A way to see what the scan finds without the menu, which is also how it gets tested on a
// machine with a real collection on it rather than on the handful of files a test can make.
CCMD( fua_wadlibrary )
{
	using namespace zx::wadlibrary;

	const unsigned int started = I_MSTime( );

	Begin( true );

	Printf( "Scanning...\n" );

	// The scan is a worker, and this command is a single call on the main thread -- so it waits,
	// which nothing on the menu path ever does.
	for ( int i = 0; i < 6000; ++i )
	{
		Tick( );
		if ( State( ) != ScanState::Running )
			break;
		I_Sleep( 5 );
	}

	const unsigned int scanned = I_MSTime( );

	if ( State( ) != ScanState::Done )
	{
		Printf( TEXTCOLOR_ORANGE "Scan did not finish.\n" TEXTCOLOR_NORMAL );
		return;
	}

	const std::vector<zx::LibraryFile> &files = Files( );
	// Not `key`: the CCMD macro already has one in scope, and shadowing it compiles on some
	// toolchains and not others.
	const std::string query = ( argv.argc( ) >= 2 ) ? zx::SearchFold( argv[1] ) : std::string( );

	// [rc4l] Ten times, because one pass over even a large collection is faster than the clock this
	// is measured with and would print zero however long it really took.
	const unsigned int beforeRows = I_MSTime( );
	std::vector<zx::LibraryRow> rows;
	for ( int i = 0; i < 10; ++i )
		rows = zx::BuildLibraryRows( files, query );
	const unsigned int afterRows = I_MSTime( );

	Printf( "%d file(s) found, %d row(s) after merging copies%s\n",
		static_cast<int>( files.size( )), static_cast<int>( rows.size( )),
		HitCap( ) ? ", STOPPED AT THE CAP" : "" );
	Printf( "scan %u ms, filter+dedup+sort %.1f ms per pass\n",
		scanned - started, ( afterRows - beforeRows ) / 10.0f );

	const size_t show = ( rows.size( ) < 40 ) ? rows.size( ) : 40;
	for ( size_t i = 0; i < show; ++i )
	{
		const zx::LibraryFile &f = files[rows[i].index];
		Printf( "  %-44s %9d  %s%s\n", f.name.c_str( ), static_cast<int>( f.size ),
			f.folder.c_str( ), ( rows[i].copies > 1 ) ? "  (+copies)" : "" );
	}

	if ( show < rows.size( ))
		Printf( "  ... and %d more\n", static_cast<int>( rows.size( ) - show ));
}
