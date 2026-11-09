// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The folder picker on Windows: the shell's folder dialog.

#include "features/wad-folders/zx_folderpicker.h"

#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>

extern HWND Window;

namespace zx { namespace wadfolders {

PickResult PickFolderNative(void *owner, const char *title, std::string &out)
{
	const HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	PickResult result = PickResult::Unavailable;

	IFileOpenDialog *dialog = NULL;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
	{
		result = PickResult::Cancelled;
		DWORD options = 0;
		dialog->GetOptions(&options);
		dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

		wchar_t wideTitle[256];
		if (MultiByteToWideChar(CP_ACP, 0, title, -1, wideTitle, 256) > 0)
			dialog->SetTitle(wideTitle);

		IShellItem *item = NULL;
		if (SUCCEEDED(dialog->Show(static_cast<HWND>(owner))) && SUCCEEDED(dialog->GetResult(&item)))
		{
			PWSTR path = NULL;
			if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
			{
				// [rc4l] The engine opens files by ANSI name, so a folder the code page cannot spell is refused rather than mangled.
				char narrow[MAX_PATH * 2];
				BOOL lossy = FALSE;
				if (WideCharToMultiByte(CP_ACP, 0, path, -1, narrow, sizeof(narrow), NULL, &lossy) > 0 && !lossy)
				{
					out = narrow;
					result = PickResult::Picked;
				}
				CoTaskMemFree(path);
			}
			item->Release();
		}
		dialog->Release();
	}

	if (SUCCEEDED(init))
		CoUninitialize();
	return result;
}

void *MainWindowHandle()
{
	return Window;
}

}} // namespace zx::wadfolders
