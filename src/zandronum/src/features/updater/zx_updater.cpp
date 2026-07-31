// [rc4l] See zx_updater.h. Notice state + a CCMD to drive it until the background check exists.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/updater/zx_updater.h"

#include "zstring.h"
#include "c_dispatch.h"   // CCMD
#include "gitinfo.h"      // GIT_DESCRIPTION (the running build's tag)
#include "features/updater/computation/release_url_compute.h"

namespace zx { namespace updater {

namespace {
bool g_available = false;
FString g_tag;

// The running build's clean tag, e.g. "v0.1.18" from "v0.1.18-37-g...". Empty if it can't be parsed.
FString CurrentTag()
{
	char buf[64];
	if (zx::ExtractVersionTag(GIT_DESCRIPTION, buf, sizeof buf))
		return FString(buf);
	return FString();
}
} // namespace

void SetLatestTag(const char *tag)
{
	if (tag == NULL || tag[0] == '\0' || !zx::IsNewerVersion(CurrentTag().GetChars(), tag))
	{
		Clear();
		return;
	}
	g_available = true;
	g_tag = tag;
}

void Clear()
{
	g_available = false;
	g_tag = "";
}

bool IsAvailable()
{
	return g_available;
}

const char *Tag()
{
	return g_tag.GetChars();
}

} } // namespace zx::updater

// [rc4l] Stand-in for the background release check: mark `tag` as the latest available so the main-menu
// notice appears (only if it is actually newer than this build). `update_notify_clear` hides it again.
CCMD(update_notify)
{
	if (argv.argc() < 2)
	{
		Printf("usage: update_notify <tag>   (e.g. update_notify v0.1.19)\n");
		return;
	}
	zx::updater::SetLatestTag(argv[1]);
	if (zx::updater::IsAvailable())
		Printf("update notice armed for %s\n", zx::updater::Tag());
	else
		Printf("no notice: %s is not newer than this build\n", argv[1]);
}

CCMD(update_notify_clear)
{
	zx::updater::Clear();
	Printf("update notice cleared\n");
}
