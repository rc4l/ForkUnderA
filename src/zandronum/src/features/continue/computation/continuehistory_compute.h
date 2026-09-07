// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The last several things the player did, rather than the last one.
//
// Continue used to remember exactly two sessions -- the last server and the last offline game -- and
// pressing the pill acted on whichever was newer. That is right until the player has more than two
// things going: a campaign half finished, the server they play on most evenings, and the map they
// were testing an hour ago are all "where I left off", and remembering two of them means the third
// is destroyed by whichever of the other two they touch next.
//
// So the record becomes a LIST, capped, and the pill opens it. What the list has to get right:
//
// UNIQUENESS IS BY WHAT IT IS, NOT WHEN IT HAPPENED. Playing the same server three evenings running
// is one thing done three times, and a history that shows it three times has spent three of its
// rows saying the same sentence. Each entry therefore carries an IDENTITY -- the address, or the map
// and the files it was played with -- and a fresh session with an identity already in the list
// REPLACES that row and moves it to the top rather than adding a second one.
//
// ORDER COMES FROM THE COUNTER, DISPLAY FROM THE CLOCK. `stamp` is monotonic and owned by us;
// `playedAt` is the system clock and is not. Sorting by the clock means a machine whose time is
// wrong -- or which corrects itself while the engine runs -- reorders a list the player has learned
// the shape of. Sorting by the counter cannot do that, and the clock is still the only thing that
// can say "yesterday", so both are kept and each does the one job it can be trusted with.
//
// A CORRUPT ENTRY COSTS ONE ROW. The file holds many records where it used to hold one, so parsing
// it all-or-nothing would mean a single mangled entry throwing away the other forty-nine. Anything
// that does not parse is skipped and the rest of the file is read.
//
// Header-pure by the features/ rules: no engine types, and the clock arrives as a parameter so this
// unit can be tested at a fixed instant.

#ifndef ZX_CONTINUEHISTORY_COMPUTE_H
#define ZX_CONTINUEHISTORY_COMPUTE_H

#include "features/continue/computation/continuerecord_compute.h"

#include <string>
#include <vector>

namespace zx
{

// How many entries the player may ask to keep.
//
// [rc4l] The floor is ONE, not zero. Zero entries is not a smaller history, it is the feature turned
// off, and a size control that switches something off at one end of its travel is two settings
// wearing one hat -- somebody who wanted a shorter list gets no list at all and no way to tell which
// of the two they asked for.
const int kContinueHistoryMin = 1;
const int kContinueHistoryMax = 50;
const int kContinueHistoryDefault = 10;

// What a requested size actually means. Anything outside the range is pulled to the nearest end
// rather than refused: the value arrives from a cvar, and a cvar can hold whatever a player typed
// into the console.
int ClampContinueHistoryLimit(int requested);

// [rc4l] What makes two sessions the same session, as a string that can be compared.
//
// A server is its address -- the same address is the same server whatever it renamed itself to since.
// A local game is the map AND the files it was played with, because MAP01 of one megawad is not
// MAP01 of another, and a history that thought otherwise would overwrite one with the other. A
// hosted game is the settings that would start it again.
//
// Empty for a record with nothing to continue, which is never inserted.
std::string ContinueIdentity(const ContinueRecord &record);

// The activity column: what the player would call this row.
std::string ContinueEntryLabel(const ContinueRecord &record);

// The "last played" column, relative to now. Both times are seconds since the epoch.
//
// [rc4l] Relative rather than a date, because the question a player is answering while looking at
// this list is "which of these is the one I was just in", and "14:32" only answers that if they
// remember what time it was when they stopped.
std::string FormatLastPlayed(long long nowEpoch, long long thenEpoch);

// The entry already in the history with this identity, or NULL. Used before a snapshot is written,
// so a session that is replacing a row can overwrite that row's save file instead of leaving it
// behind with nothing pointing at it.
const ContinueRecord *FindContinueEntry(const std::vector<ContinueRecord> &history,
	const std::string &identity);

// Newest first and no longer than the limit. Applied on the way in AND on the way out, so lowering
// the limit takes effect on the next read rather than the next write.
std::vector<ContinueRecord> TrimContinueHistory(const std::vector<ContinueRecord> &history, int limit);

// The history with this session in it: replacing the row that means the same thing, at the top,
// capped. A record with nothing to continue is returned unchanged rather than inserted.
std::vector<ContinueRecord> InsertContinueEntry(const std::vector<ContinueRecord> &history,
	const ContinueRecord &entry, int limit);

// The history without the entry at `index`, for the player who wants one row gone. Out of range
// leaves it alone.
std::vector<ContinueRecord> RemoveContinueEntry(const std::vector<ContinueRecord> &history, int index);

// One past the highest stamp in the list, so a new entry is newer than everything already there.
int NextContinueStamp(const std::vector<ContinueRecord> &history);

// The file. Version stated once for the whole list rather than per entry, so a file cannot disagree
// with itself about what format it is in.
const int kContinueHistoryFormat = 1;

std::string SerialiseContinueHistory(const std::vector<ContinueRecord> &history);

// False only when the file is not one of ours or is from a build we cannot read. Entries that do not
// parse are dropped and the rest are kept, so one bad row is one bad row.
bool ParseContinueHistory(const std::string &text, std::vector<ContinueRecord> &out);

} // namespace zx

#endif // ZX_CONTINUEHISTORY_COMPUTE_H
