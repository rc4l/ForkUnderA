// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which custom vote submenus the call vote menu still has to add, kept apart from the menu items it reads.

#ifndef ZX_CALLVOTEMENU_COMPUTE_H
#define ZX_CALLVOTEMENU_COMPUTE_H

#include <string>
#include <vector>

namespace zx {

// [rc4l] Adapted from zandronum@a84419a6841a8bf9384adae34e97104ef00f9027: the indices of voteMenus to add, each named and not already an action of the menu's items (names compared as FName does, ignoring case), so opening the menu again adds none twice.
std::vector<unsigned> ComputeMissingVoteMenus(const std::vector<std::string> &itemActions, const std::vector<std::string> &voteMenus);

} // namespace zx

#endif // ZX_CALLVOTEMENU_COMPUTE_H
