// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_ACSQUERIES_H
#define ZX_ACSQUERIES_H

#include <cstdint>
#include <cstdio>
#include <vector>

#include "features/acs-queries/computation/queries_compute.h"

class FArchive;
struct PNGHandle;

namespace zx { namespace acsqueries {

// [Doruk-Aksoy] fua_QueryCreate: a new query of the calling script, 0 when it already holds 16 (warned once a map).
int32_t QueryCreate(ScriptQueries &queries);
void QueryFree(ScriptQueries &queries, int32_t id);
void QueryClear(ScriptQueries &queries, int32_t id, int kind);

// [Doruk-Aksoy] fua_QueryFilter with `count` arguments after its kind, `name` the first one's string for class and flag filters.
bool QueryFilter(ScriptQueries &queries, int32_t id, int kind, int count, int32_t a, int32_t b, const char *name);
void QuerySort(ScriptQueries &queries, int32_t id, int mode, int32_t max);

// [Doruk-Aksoy] fua_FindActors: the blockmap's actors in range of an ACS fixed-point centre, the query's results replaced; returns their count.
int FindActors(ScriptQueries &queries, int32_t id, int32_t x, int32_t y, int32_t z, int32_t radius);

// [Doruk-Aksoy] fua_FindActorsOfClass: every actor of the class on the map, sorted from the centre when one is given.
int FindActorsOfClass(ScriptQueries &queries, int32_t id, const char *cls, bool subclasses, const int32_t *centre);

// [Doruk-Aksoy] fua_FoundActor: the i-th result's handle, 0 once its actor is gone; fua_FoundCount: how many results.
int32_t FoundActor(const ScriptQueries &queries, int32_t id, int index);
int FoundCount(const ScriptQueries &queries, int32_t id);

// [Doruk-Aksoy] From DLevelScript::Serialize: the script's queries, results included.
void SerializeScript(FArchive &arc, ScriptQueries &queries);

// [Doruk-Aksoy] The next script-object id; queries and tracers share one serial, so their ids never meet.
int32_t NextObjectId();

// [Doruk-Aksoy] A loaded script holds `id`, so the serial goes on past it.
void NoteObjectId(int32_t id);

// [Doruk-Aksoy] A script object's run of ints and the names it holds, saved as names since name numbers change between runs.
void SerializeNamedRun(FArchive &arc, std::vector<int32_t> &names, std::vector<int32_t> &flat);

// [Doruk-Aksoy] The next query id in savegames, so no saved id is granted again.
void WriteIds(FILE *file);
void ReadIds(PNGHandle *png);

// [Doruk-Aksoy] A new game: no script holds a query, so ids start over.
void ResetIds();

// [Doruk-Aksoy] A level was entered, so the full-script warning may show again.
void EnterLevel();

}} // namespace zx::acsqueries

#endif
