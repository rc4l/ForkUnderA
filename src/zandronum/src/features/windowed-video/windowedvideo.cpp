// [rc4l] windowed-video: the vid_setsize console command, cross-platform.
//
// Faithful to upstream's vid_setsize. With two args it sets a specific windowed size; with none it
// re-applies the persisted vid_defwidth/vid_defheight (used by the "Apply windowed size" menu
// command). The actual OS-window resize is done by DFrameBuffer::SetWindowSize, overridden by the
// SDL and Win32 framebuffers -- so this glue is platform-neutral.
//
// >>> SUPERSEDED-BY-UPSTREAM <<< See features/windowed-video/README.md.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "c_dispatch.h"
#include "c_cvars.h"
#include "v_video.h"

EXTERN_CVAR (Int, vid_defwidth)
EXTERN_CVAR (Int, vid_defheight)

CCMD (vid_setsize)
{
	int w, h;
	if (argv.argc () >= 3)
	{
		w = atoi (argv[1]);
		h = atoi (argv[2]);
		vid_defwidth = w;
		vid_defheight = h;
	}
	else
	{
		// No args: re-apply the persisted windowed size (the menu's "Apply windowed size").
		w = vid_defwidth;
		h = vid_defheight;
	}

	if (w < 320) w = 320;
	if (h < 200) h = 200;

	if (screen != NULL)
		screen->SetWindowSize (w, h);
}
