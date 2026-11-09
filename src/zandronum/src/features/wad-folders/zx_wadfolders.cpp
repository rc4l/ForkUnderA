// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_wadfolders.h; the list, the commands and the menu are main thread only, the walk runs on a worker that touches nothing of the engine's.

#include "features/wad-folders/zx_wadfolders.h"
#include "features/wad-folders/zx_folderpicker.h"
#include "features/wad-folders/computation/wadfolders_compute.h"
#include "features/wad-library/computation/wadlibrary_compute.h"   // [rc4l] LibraryDepthCap, LibraryFileCap
#include "features/handoff/effects.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "c_console.h"
#include "c_dispatch.h"
#include "cmdlib.h"
#include "doomtype.h"
#include "gameconfigfile.h"
#include "i_system.h"
#include "menu/menu.h"
#include "v_font.h"
#include "v_video.h"
#include "v_palette.h"
#include "c_bind.h"
#include "d_event.h"
#include "d_gui.h"
#include "gi.h"
#include "menu/optionmenuitems.h"
#include "v_text.h"
#include "zstring.h"

namespace zx { namespace wadfolders {

namespace
{

const char *const kSection = "fua_WadFolders";
const char *const kMenu = "FUAWadFolders";
const double kRefreshAgeSeconds = 60.0;
// [rc4l] What a menu row holds before the option menu's right-aligned label runs off the left edge.
const size_t kMenuLabelChars = 40;

#ifdef _WIN32
const bool kIgnoreCase = true;
#else
const bool kIgnoreCase = false;
#endif

std::mutex g_mutex;
std::shared_ptr<const WadFolderIndex> g_index;
std::chrono::steady_clock::time_point g_builtAt;
std::thread g_worker;
std::atomic<bool> g_cancel(false);
std::atomic<bool> g_hitCap(false);
bool g_termRegistered = false;

// [rc4l] One folder's entries from the disk, which the worker may call: no FString, no engine state.
bool ListDisk(const std::string &dir, std::vector<WadFolderEntry> &out)
{
	const std::string prefix = (!dir.empty() && dir[dir.size() - 1] == '/') ? dir : dir + "/";
#ifdef _WIN32
	findstate_t entry;
	void *handle = I_FindFirst((prefix + "*").c_str(), &entry);
	if (handle == (void *)-1)
		return false;
	do
	{
		const std::string name = I_FindName(&entry);
		if (name.empty() || name == "." || name == "..")
			continue;
		out.push_back(WadFolderEntry{ name, (I_FindAttr(&entry) & FA_DIREC) != 0 });
	} while (I_FindNext(handle, &entry) == 0);
	I_FindClose(handle);
	return true;
#else
	DIR *handle = opendir(dir.c_str());
	if (handle == NULL)
		return false;
	while (dirent *ent = readdir(handle))
	{
		const std::string name = ent->d_name;
		if (name.empty() || name == "." || name == "..")
			continue;
		struct stat info;
		if (stat((prefix + name).c_str(), &info) != 0)
			continue;
		out.push_back(WadFolderEntry{ name, S_ISDIR(info.st_mode) != 0 });
	}
	closedir(handle);
	return true;
#endif
}

void StopWorker()
{
	if (g_worker.joinable())
	{
		g_cancel = true;
		g_worker.join();
	}
}

void WriteFolders(const std::vector<std::string> &folders)
{
	if (GameConfig == NULL || !GameConfig->SetSection(kSection, true))
		return;
	GameConfig->ClearCurrentSection();
	for (size_t i = 0; i < folders.size(); ++i)
		GameConfig->SetValueForKey("Path", folders[i].c_str(), true);
}

double IndexAgeSeconds()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_builtAt).count();
}

// [rc4l] Runs `fn` here, or in the handoff when a tic on its own thread ran the command, since it opens dialogs and writes the ini.
void OnMainThread(void (*fn)(intptr_t, intptr_t), intptr_t a)
{
	zx::handoff::UIEffect effect = { fn, a, 0 };
	zx::handoff::RunOrQueue(effect);
}

void SayAdded(bool added, const char *folder)
{
	if (added)
		Printf("Searching %s for WADs, subfolders too.\n", folder);
	else
		Printf(TEXTCOLOR_ORANGE "Not added: %s is not a folder, or is listed already.\n", folder);
}

} // namespace

std::vector<std::string> Folders()
{
	std::vector<std::string> out;
	if (GameConfig == NULL || !GameConfig->SetSection(kSection))
		return out;
	const char *key, *value;
	while (GameConfig->NextInSection(key, value))
	{
		if (stricmp(key, "Path") == 0 && value[0] != '\0')
			out.push_back(value);
	}
	return out;
}

std::vector<std::string> SearchRoots()
{
	std::vector<std::string> roots;
	const std::vector<std::string> folders = Folders();
	for (size_t i = 0; i < folders.size(); ++i)
	{
		FString dir = NicePath(folders[i].c_str());
		FixPathSeperator(dir);
		if (dir.IsEmpty() || !DirEntryExists(dir))
			continue;
		ComputeWadFolderAdd(roots, dir.GetChars(), kIgnoreCase);
	}
	return roots;
}

bool Add(const char *folder)
{
	if (folder == NULL || folder[0] == '\0')
		return false;
	FString expanded = NicePath(folder);
	FixPathSeperator(expanded);
	if (!DirEntryExists(expanded))
		return false;

	std::vector<std::string> folders = Folders();
	if (!ComputeWadFolderAdd(folders, folder, kIgnoreCase))
		return false;
	WriteFolders(folders);
	StartIndex(true);
	RefreshMenu();
	return true;
}

bool Remove(size_t index)
{
	std::vector<std::string> folders = Folders();
	if (!ComputeWadFolderRemove(folders, index))
		return false;
	WriteFolders(folders);
	StartIndex(true);
	RefreshMenu();
	return true;
}

bool PickAndAdd(void *owner)
{
	std::string folder;
	const PickResult picked = PickFolderNative(owner, "Choose a folder of WADs", folder);
	if (picked == PickResult::Unavailable)
		Printf(TEXTCOLOR_ORANGE "No folder picker here; type fua_wadfolder_add <folder> in the console.\n");
	if (picked != PickResult::Picked)
		return false;
	const bool added = Add(folder.c_str());
	SayAdded(added, folder.c_str());
	return added;
}

void StartIndex(bool force)
{
	if (!force)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_index || g_worker.joinable())
			return;
	}
	StopWorker();

	const std::vector<std::string> roots = SearchRoots();
	if (roots.empty())
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_index.reset();
		return;
	}

	if (!g_termRegistered)
	{
		atterm(StopWorker);
		g_termRegistered = true;
	}

	g_cancel = false;
	g_worker = std::thread([roots]() {
		std::shared_ptr<WadFolderIndex> built = std::make_shared<WadFolderIndex>();
		const bool hitCap = ComputeWadFolderWalk(roots, ListDisk, LibraryDepthCap(), LibraryFileCap(),
			[]() { return g_cancel.load(); }, *built);
		if (g_cancel.load())
			return;
		std::lock_guard<std::mutex> lock(g_mutex);
		g_index = built;
		g_builtAt = std::chrono::steady_clock::now();
		g_hitCap = hitCap;
	});
}

void FindAll(const char *name, std::vector<std::string> &out)
{
	if (name == NULL || name[0] == '\0')
		return;
	std::shared_ptr<const WadFolderIndex> index;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		index = g_index;
	}
	if (index)
		index->Find(name, out);
}

void PrepareLookup(const char *name)
{
	if (g_worker.joinable())
		g_worker.join();

	std::vector<std::string> found;
	FindAll(name, found);
	if (!ComputeWadFolderNeedsRefresh(found.empty(), IndexAgeSeconds(), kRefreshAgeSeconds) || SearchRoots().empty())
		return;

	StartIndex(true);
	if (g_worker.joinable())
		g_worker.join();
}

void RefreshMenu()
{
	FMenuDescriptor **desc = MenuDescriptors.CheckKey(FName(kMenu));
	if (desc == NULL || (*desc)->mType != MDESC_OptionsMenu)
		return;

	// [rc4l] The items a rebuild replaces are freed by the next one, since the item whose command added or removed a folder is still running.
	static std::vector<FOptionMenuItem *> retired;
	for (size_t i = 0; i < retired.size(); ++i)
		delete retired[i];
	retired.clear();

	FOptionMenuDescriptor *opt = static_cast<FOptionMenuDescriptor *>(*desc);
	for (unsigned i = 0; i < opt->mItems.Size(); ++i)
		retired.push_back(opt->mItems[i]);
	opt->mItems.Clear();

	opt->mItems.Push(new FOptionMenuItemStaticText("Searched, subfolders too, before downloading", false));
	opt->mItems.Push(new FOptionMenuItemStaticText(" ", false));
	opt->mItems.Push(new FOptionMenuItemCommand("Add a folder...", "fua_wadfolder_pick", false));
	opt->mItems.Push(new FOptionMenuItemStaticText(" ", false));

	const std::vector<std::string> folders = Folders();
	if (folders.empty())
	{
		opt->mItems.Push(new FOptionMenuItemStaticText("No folders yet", false));
	}
	else
	{
		opt->mItems.Push(new FOptionMenuItemStaticText("Select a folder to stop searching it", true));
		for (size_t i = 0; i < folders.size(); ++i)
		{
			FString command;
			command.Format("fua_wadfolder_remove %u", static_cast<unsigned>(i));
			const std::string label = ComputeWadFolderLabel(folders[i], kMenuLabelChars);
			opt->mItems.Push(new FOptionMenuItemSafeCommand(label.c_str(), command, false));
		}
	}

	if (opt->mSelectedItem >= static_cast<int>(opt->mItems.Size()))
		opt->mSelectedItem = static_cast<int>(opt->mItems.Size()) - 1;
	opt->CalcIndent();
}

}} // namespace zx::wadfolders

// [rc4l] Lists the WAD folders and how many files their index holds.
CCMD(fua_wadfolders)
{
	const std::vector<std::string> folders = zx::wadfolders::Folders();
	if (folders.empty())
	{
		Printf("No WAD folders. Add one with fua_wadfolder_add <folder> or Options > FUA Options > WAD Folders.\n");
		return;
	}
	for (size_t i = 0; i < folders.size(); ++i)
		Printf("%u. %s\n", static_cast<unsigned>(i), folders[i].c_str());
	if (zx::wadfolders::g_hitCap.load())
		Printf(TEXTCOLOR_ORANGE "The index stopped at %u files; the rest of these folders are not searched.\n",
			static_cast<unsigned>(zx::LibraryFileCap()));
}

// [rc4l] Unsafe so a mod cannot point the search at a folder or open dialogs; the menu's own items run it trusted.
UNSAFE_CCMD(fua_wadfolder_add)
{
	if (argv.argc() < 2)
	{
		Printf("Usage: fua_wadfolder_add <folder>\n");
		return;
	}
	zx::wadfolders::OnMainThread([](intptr_t a, intptr_t) {
		std::unique_ptr<std::string> folder(reinterpret_cast<std::string *>(a));
		zx::wadfolders::SayAdded(zx::wadfolders::Add(folder->c_str()), folder->c_str());
	}, reinterpret_cast<intptr_t>(new std::string(argv[1])));
}

UNSAFE_CCMD(fua_wadfolder_remove)
{
	if (argv.argc() < 2)
	{
		Printf("Usage: fua_wadfolder_remove <number from fua_wadfolders>\n");
		return;
	}
	zx::wadfolders::OnMainThread([](intptr_t a, intptr_t) {
		if (!zx::wadfolders::Remove(static_cast<size_t>(a)))
			Printf(TEXTCOLOR_ORANGE "No WAD folder %u.\n", static_cast<unsigned>(a));
	}, static_cast<intptr_t>(strtoul(argv[1], NULL, 10)));
}

UNSAFE_CCMD(fua_wadfolder_pick)
{
	zx::wadfolders::OnMainThread([](intptr_t, intptr_t) {
		zx::wadfolders::PickAndAdd(zx::wadfolders::MainWindowHandle());
	}, 0);
}

// [rc4l] Opens the WAD Folders menu with its list current.
CCMD(fua_wadfolders_menu)
{
	zx::wadfolders::RefreshMenu();
	M_SetMenu(FName("FUAWadFolders"));
}
