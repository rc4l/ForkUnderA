// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Keeping FUA's own menu entries alive when a mod replaces the menu they live in.
//
// A mod that wants one line in the options menu cannot add one line: MENUDEF has no way to say
// "insert into OptionsMenu", so the convention is to copy the whole stock OptionsMenu into your own
// MENUDEF and paste your entry in. Ghouls vs Humans: Legacy of Darkness does exactly that
// (lod-patchv1.7a.pk3/MENUDEF), and its copy was written against stock Zandronum, so it has no FUA
// Options line. Mod MENUDEFs parse after ours, their definition replaces ours wholesale, and every
// FUA setting silently disappears from the game.
//
// Nothing is wrong with what the mod did. It is the only thing MENUDEF lets them do, they did it
// years before this fork existed, and the same is true of every other mod that touches the options
// menu. So the engine re-pins our entry afterwards instead of expecting mods to know about us.

#ifndef ZX_MENUPIN_H
#define ZX_MENUPIN_H

namespace zx
{

// Put "FUA Options" back at the top of OptionsMenu if whatever parsed last does not have it.
// Called once, after every MENUDEF has been read, since that is the only moment the answer is
// knowable. A no-op when our entry survived, which is the common case.
void MenuPin_RestoreFuaOptions();

} // namespace zx

#endif
