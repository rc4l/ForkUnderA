// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The folder picker on Linux and the BSDs: zenity, else kdialog.

#include "features/wad-folders/zx_folderpicker.h"

#include <cstdio>
#include <cstddef>
#include <string>

#include <sys/wait.h>

namespace zx { namespace wadfolders {

namespace
{

// [rc4l] Runs one picker command, 127 from the shell meaning the program is not installed.
PickResult RunPicker(const std::string &command, std::string &out)
{
	FILE *pipe = popen(command.c_str(), "r");
	if (pipe == NULL)
		return PickResult::Unavailable;

	std::string text;
	char buffer[512];
	while (fgets(buffer, sizeof(buffer), pipe) != NULL)
		text += buffer;

	const int status = pclose(pipe);
	if (!WIFEXITED(status) || WEXITSTATUS(status) == 127)
		return PickResult::Unavailable;
	while (!text.empty() && (text[text.size() - 1] == '\n' || text[text.size() - 1] == '\r'))
		text.erase(text.size() - 1);
	if (WEXITSTATUS(status) != 0 || text.empty())
		return PickResult::Cancelled;
	out = text;
	return PickResult::Picked;
}

} // namespace

PickResult PickFolderNative(void *, const char *, std::string &out)
{
	// [rc4l] Fixed command lines, nothing of the caller's in them, so there is nothing to quote.
	const PickResult zenity = RunPicker("zenity --file-selection --directory --title='Choose a folder of WADs' 2>/dev/null", out);
	if (zenity != PickResult::Unavailable)
		return zenity;
	return RunPicker("kdialog --getexistingdirectory ~ --title 'Choose a folder of WADs' 2>/dev/null", out);
}

void *MainWindowHandle()
{
	return NULL;
}

}} // namespace zx::wadfolders
