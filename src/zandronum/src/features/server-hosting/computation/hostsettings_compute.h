// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Which of a hand-built server's settings may be written into the preset folder that
// remembers it.
//
// The NEW tab already keeps everything its settings boxes decide as name/value pairs, and
// customsave_compute turns that list into the server.cfg of a saved preset -- so a setting expressed
// as a cvar is persisted, carried into EDIT and handed to the next player who is given the folder,
// with no schema of its own. That is exactly what the server name, the player limit and the
// visibility choice want.
//
// It is exactly what two of them do NOT want, and there is no way to tell from the list itself:
//
//   * THE PASSWORD. The hosting form has never remembered one, deliberately -- "a password saved in
//     a config file that anyone with the machine can read is a worse promise than no password" --
//     and a preset folder is that config file plus the expectation that it can be handed to
//     somebody. Routing the password through the same list as the rest would quietly reverse a
//     decision the other screen made on purpose.
//   * THE PORT. A preset describes a game, not a machine, and the whole point of the folder shape
//     is that one player can hand it to another and it works. A port travelling inside it would
//     arrive as somebody else's answer to a question only the receiving machine can answer.
//
// So scope is a property of the NAME, decided in one place and tested, rather than a filter each
// caller remembers to apply. A setting that is not explicitly transient or machine-scoped is saved,
// which is the safe default for the persistence question and the reason the two exceptions are
// listed rather than the rule.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_HOSTSETTINGS_COMPUTE_H
#define ZX_HOSTSETTINGS_COMPUTE_H

#include <string>
#include <utility>
#include <vector>

namespace zx
{

// [rc4l] Where a settings row's value lives, which decides both who reads it back and whether it
// survives being handed to somebody else.
enum class SettingScope
{
	// [rc4l] Written into the preset's server.cfg, so it is what EDIT shows and what the folder
	// carries.
	Preset,

	// [rc4l] An archived client cvar belonging to this installation, never written to a preset.
	Machine,

	// [rc4l] Held for as long as the screen is open and written nowhere at all.
	Session,
};

// [rc4l] The scope of one settings row, by cvar name.
SettingScope ComputeSettingScope(const std::string &name);

// [rc4l] The pairs a saved preset may contain, which is every Preset-scoped one in the order given.
std::vector<std::pair<std::string, std::string> > ComputeSavedCvars(
	const std::vector<std::pair<std::string, std::string> > &cvars);

// [rc4l] The player limit a server may actually be started with.
//
// The box offers a slider, but a preset folder is a text file somebody can edit and a hand-written
// sv_maxclients of 0 is a server nobody can join -- so the value is corrected where it is read
// rather than trusted because our own slider could not have produced it.
int ComputeClampedMaxPlayers(int wanted);

// [rc4l] What a hand-built server is called when nobody has said otherwise.
//
// A fixed name rather than one derived from the load order: the files can change under a saved
// preset, and a server that renames itself because you reordered its wads is answering a question
// nobody asked. The form shows this rather than an empty box, so what is read is what is hosted.
extern const char *const kFuaDefaultBuildServerName;

// [rc4l] The name a hand-built server runs under, given what the player typed.
//
// Empty -- or nothing but spaces, which is an empty name that does not look like one -- falls back
// to the default above rather than to a blank server nobody can pick out of a browser.
std::string ComputeHostDisplayName(const std::string &typed);

} // namespace zx

#endif // ZX_HOSTSETTINGS_COMPUTE_H
