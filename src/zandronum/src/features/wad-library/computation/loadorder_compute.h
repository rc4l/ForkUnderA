// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The list of files a hand-built server will load, and the four things you can do to it.
//
// It is a LIST AND NOT A SET, because order is the whole point: a patch has to load after what it
// patches, and an override after what it overrides. Every rule below follows from that.
//
// ONE FILE OF EACH NAME. The engine identifies a loaded file to a joining client BY NAME, so two
// files called map01.wad in one server are two things the client cannot tell apart -- it is told to
// find "map01.wad" twice and has no way to know which of them it already has. Refusing the second
// is not tidiness; it is the difference between a server people can join and one they cannot.
//
// AND THAT IS A REFUSAL ABOUT THE NAME, NOT THE CONTENT. Two different builds of one mod share a
// name and differ in every byte, and the answer is still no, because the client's problem is the
// same either way. The caller says which one it wanted; this says only that it cannot have both.
//
// THE SAME FILE TWICE IS A DIFFERENT QUESTION, and also no: adding a file already in the list is a
// misclick, and the list is short enough that silently doing nothing would look broken. The caller
// gets told which of the two it was, so it can say so.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_LOADORDER_COMPUTE_H
#define ZX_LOADORDER_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// One file in the list. The PATH is what gets loaded and the name is what is shown; keeping both is
// what stops the resolution being redone later against a different copy of the same name.
struct LoadOrderEntry
{
	std::string path;
	std::string name;
	std::string md5;		// empty until it has been hashed, which happens when it is added
	long long size;

	LoadOrderEntry() : size(0) {}
	LoadOrderEntry(const std::string &p, const std::string &n, long long s)
		: path(p), name(n), size(s) {}
};

enum class AddVerdict
{
	Added,

	// Already in the list, the very same file. A misclick.
	AlreadyThere,

	// A DIFFERENT file of the same name. The one that has to be explained, because the player can
	// see two rows that look different and is being told they are the same to a client.
	NameTaken,

	// Nothing to add.
	Empty,
};

struct AddResult
{
	AddVerdict verdict;
	size_t index;		// where it landed, or where the one already holding the name sits

	AddResult() : verdict(AddVerdict::Empty), index(0) {}
	AddResult(AddVerdict v, size_t i) : verdict(v), index(i) {}
};

// Append `entry`, or say why not. Appends rather than inserts: a file added later loads later, which
// is what somebody building a list from the top down means by adding one.
AddResult AddToLoadOrder(std::vector<LoadOrderEntry> &list, const LoadOrderEntry &entry);

// Move the entry at `index` one place towards the front or the back. Returns where it ended up,
// which is where it started when it was already at that end.
size_t MoveInLoadOrder(std::vector<LoadOrderEntry> &list, size_t index, int step);

// Drop the entry at `index`. Returns the row the selection should sit on afterwards: the one that
// took its place, or the new last row when it was the last.
size_t RemoveFromLoadOrder(std::vector<LoadOrderEntry> &list, size_t index);

// [rc4l] What the server is actually told, in order: the IWAD first and then every file.
//
// The IWAD is not IN the list, and this is where that shows: it is one choice rather than a member
// of an ordered set, so keeping it out of the list means the list cannot be reordered into a state
// where it is not first.
std::vector<std::string> LoadOrderPaths(const std::string &iwadPath,
	const std::vector<LoadOrderEntry> &list);

} // namespace zx

#endif // ZX_LOADORDER_COMPUTE_H
