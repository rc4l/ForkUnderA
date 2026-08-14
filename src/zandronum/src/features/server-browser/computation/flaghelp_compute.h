// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] One line about what each server flag does.
//
// The FLAGS box lists 175 switches by their cvar names, and a cvar name is not an explanation:
// sv_nocountendmonst, compat_plasmabump and compat_badangles say nothing to somebody deciding
// whether to tick them. So every flag carries a sentence, and the box shows it on hover.
//
// WHERE THE WORDING COMES FROM. Zandronum's own menudef.txt already labels most of these, and its
// history file documents the rest at the commit that added them. Those are the engine's words for
// its own behaviour, which is a better source than a guess -- so each line here is that label
// turned into a sentence rather than an independent claim about what the engine does.
//
// The rule for every line: one sentence, the fewest words that carry the meaning, describing what
// is true WHEN THE SWITCH IS ON. A flag named "no something" therefore reads as the restriction it
// imposes, because that is what ticking it does.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_FLAGHELP_COMPUTE_H
#define ZX_FLAGHELP_COMPUTE_H

#include <string>
#include <utility>
#include <vector>

namespace zx
{

// What `name` does when it is on. Empty for a flag this build has that nobody has written a line
// for, which the caller shows as no tooltip rather than as a blank box.
const char *FlagHelp(const std::string &name);

// What a whole FIELD is for -- dmflags, zacompatflags and the rest -- shown on the heading that
// folds it. Same rule as the flags: one sentence, fewest words. Empty for a field with no line.
const char *FlagFieldHelp(const std::string &name);

// The whole table, in name order. Exposed so it can be checked rather than only read.
const std::vector<std::pair<std::string, std::string> > &FlagHelpTable();

} // namespace zx

#endif // ZX_FLAGHELP_COMPUTE_H
