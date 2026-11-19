// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-queries/acsqueries.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

#include "actor.h"
#include "d_player.h"
#include "dthinker.h"
#include "farchive.h"
#include "g_shared/a_pickups.h"
#include "m_bbox.h"
#include "m_fixed.h"
#include "m_png.h"
#include "name.h"
#include "p_local.h"
#include "c_console.h"
#include "thingdef/thingdef.h"
#include "features/acs-handles/acshandles.h"

// [Doruk-Aksoy] The SAVEVER from which scripts save their queries.
static const int kQuerySaveVersion = 4540;
extern int SaveVersion;

namespace zx { namespace acsqueries {

namespace {

// [Doruk-Aksoy] The last id granted on this machine; ids never repeat within a game.
int32_t g_lastId = 0;

const DWORD kChunkId = MAKE_ID('f', 'q', 'I', 'd');

// [Doruk-Aksoy] Whether this map already warned of a script out of queries.
bool g_warnedFull = false;

const char *NameOf(int32_t name)
{
	return FName(ENamedName(name)).GetChars();
}

// [Doruk-Aksoy] A flag name with an optional class prefix, looked up for one class.
FFlagDef *FlagOf(const PClass *type, const char *flag)
{
	const char *dot = strchr(flag, '.');
	if (dot == NULL)
		return FindFlag(type, flag, NULL);
	return FindFlag(type, FString(flag, dot - flag), dot + 1);
}

// [Doruk-Aksoy] Whether any actor class has the flag: the actor, inventory, weapon and player pawn lists.
bool FlagExists(const char *flag)
{
	const PClass *types[] = { RUNTIME_CLASS(AActor), RUNTIME_CLASS(AInventory), RUNTIME_CLASS(AWeapon), RUNTIME_CLASS(APlayerPawn) };
	for (const PClass *type : types)
		if (FlagOf(type, flag) != NULL)
			return true;
	return false;
}

bool IsActorClass(const PClass *type)
{
	return type != NULL && type->IsDescendantOf(RUNTIME_CLASS(AActor));
}

bool OfClass(AActor *actor, const PClass *type, bool subclasses)
{
	return subclasses ? actor->IsKindOf(type) : actor->GetClass() == type;
}

// [Doruk-Aksoy] One run of a query: its filters' classes and actors looked up once, each actor then tested in cost order.
class Run
{
public:
	explicit Run(const Query &query)
		: q(query), order(ComputeQueryFilterOrder(query.filters)), shape(ComputeQueryShape(query.filters)),
		  classes(query.filters.size(), NULL), refs(query.filters.size(), NULL), flags(query.filters.size())
	{
	}

	// [Doruk-Aksoy] False when a handle filter's actor is gone, so the query matches nothing.
	bool Resolve()
	{
		for (size_t i = 0; i < q.filters.size(); ++i)
		{
			const Filter &f = q.filters[i];
			if (f.kind == kFqClass || f.kind == kFqNotClass)
			{
				classes[i] = PClass::FindClass(FName(ENamedName(f.a)));
				if (!IsActorClass(classes[i]))
					classes[i] = NULL;
			}
			else if (ComputeFilterArg(f.kind) == kArgHandle && (refs[i] = acshandles::ActorOf(f.a)) == NULL)
				return false;
		}
		return true;
	}

	bool Matches(AActor *actor)
	{
		return ComputeQueryMatch(q.filters, order, [&](const Filter &f) { return Test(f, size_t(&f - q.filters.data()), actor); });
	}

	const Query &q;
	const std::vector<size_t> order;
	const RangeShape shape;

private:
	bool Test(const Filter &f, size_t i, AActor *actor)
	{
		switch (f.kind)
		{
		case kFqClass: return classes[i] != NULL && OfClass(actor, classes[i], f.b != 0);
		case kFqNotClass: return classes[i] == NULL || !OfClass(actor, classes[i], f.b != 0);
		case kFqAlive: return actor->health > 0;
		case kFqDead: return actor->health <= 0;
		case kFqMonster: return (actor->flags3 & MF3_ISMONSTER) != 0;
		case kFqPlayer: return actor->player != NULL && actor->player->mo == actor;
		case kFqMissile: return (actor->flags & MF_MISSILE) != 0;
		case kFqShootable: return (actor->flags & MF_SHOOTABLE) != 0;
		case kFqFlag:
		{
			FFlagDef *fd = Flag(i, actor->GetClass(), NameOf(f.a));
			return fd != NULL && (CheckActorFlag(actor, fd) != 0) == (f.b != 0);
		}
		case kFqTid: return actor->tid == f.a;
		case kFqFriendly: return refs[i]->IsFriend(actor);
		case kFqHostile: return refs[i]->IsHostile(actor);
		case kFqSight: return P_CheckSight(refs[i], actor);
		case kFqExclude: return actor != refs[i];
		default: return true;
		}
	}

	// [Doruk-Aksoy] Filter i's flag for a class, looked up once per class a run meets.
	FFlagDef *Flag(size_t i, const PClass *type, const char *name)
	{
		for (const auto &known : flags[i])
			if (known.first == type)
				return known.second;
		FFlagDef *fd = FlagOf(type, name);
		flags[i].push_back(std::make_pair(type, fd));
		return fd;
	}

	std::vector<const PClass *> classes;
	std::vector<AActor *> refs;
	std::vector<std::vector<std::pair<const PClass *, FFlagDef *>>> flags;
};

// [Doruk-Aksoy] The candidates sorted and capped, kept as the query's results by handle.
int Keep(Query &q, std::vector<Candidate> &found)
{
	ComputeQuerySort(found, q.sort, q.max);
	for (const Candidate &c : found)
		if (const int32_t h = acshandles::HandleFor(const_cast<AActor *>(static_cast<const AActor *>(c.actor))))
			q.results.push_back(h);
	return int(q.results.size());
}

// [Doruk-Aksoy] An actor's origin in map units, and its distance from `centre` by the run's shape.
double DistanceOf(AActor *actor, const double centre[3], RangeShape shape)
{
	const double origin[3] = { FIXED2DBL(actor->x), FIXED2DBL(actor->y), FIXED2DBL(actor->z) };
	return ComputeQueryDistance(centre, origin, FIXED2DBL(actor->radius), FIXED2DBL(actor->height), shape);
}

} // namespace

int32_t QueryCreate(ScriptQueries &queries)
{
	if (queries.Count() >= kMaxQueriesPerScript)
	{
		if (!g_warnedFull)
			Printf(TEXTCOLOR_ORANGE "fua_QueryCreate: a script already holds %u queries; free one with fua_QueryFree.\n", unsigned(kMaxQueriesPerScript));
		g_warnedFull = true;
		return 0;
	}
	g_lastId = ComputeNextQueryId(g_lastId);
	queries.Create(g_lastId);
	return g_lastId;
}

void QueryFree(ScriptQueries &queries, int32_t id)
{
	queries.Free(id);
}

void QueryClear(ScriptQueries &queries, int32_t id, int kind)
{
	queries.Clear(id, kind);
}

bool QueryFilter(ScriptQueries &queries, int32_t id, int kind, int count, int32_t a, int32_t b, const char *name)
{
	const ArgKind arg = ComputeFilterArg(kind);
	if (arg != kArgNone && count < 1)
		return false;
	Filter f = { kind, arg == kArgNone ? 0 : a, count >= 2 ? b : ComputeFilterDefaultB(kind) };
	bool known = true;
	if (arg == kArgName)
	{
		if (name == NULL || *name == 0)
			return false;
		f.a = FName(name).GetIndex();
		known = kind == kFqFlag ? FlagExists(name) : IsActorClass(PClass::FindClass(FName(name)));
	}
	else if (arg == kArgHandle)
		known = acshandles::ActorOf(a) != NULL;
	return queries.AddFilter(id, f) && known;
}

void QuerySort(ScriptQueries &queries, int32_t id, int mode, int32_t max)
{
	queries.SetSort(id, mode, max < 0 ? 0 : uint32_t(max));
}

int FindActors(ScriptQueries &queries, int32_t id, int32_t x, int32_t y, int32_t z, int32_t radius)
{
	Query *q = queries.Find(id);
	if (q == NULL)
		return 0;
	q->results.clear();
	Run run(*q);
	if (radius < 0 || !run.Resolve())
		return 0;
	const double centre[3] = { x / 65536.0, y / 65536.0, z / 65536.0 };
	const double range = radius / 65536.0;
	const fixed_t fx = x, fy = y, fr = radius;
	FBlockThingsIterator it(FBoundingBox(fx, fy, fr));
	std::vector<Candidate> found;
	while (AActor *actor = it.Next())
	{
		const double distance = DistanceOf(actor, centre, run.shape);
		if (distance <= range && run.Matches(actor))
			found.push_back(Candidate{ actor, distance, found.size() });
	}
	return Keep(*q, found);
}

int FindActorsOfClass(ScriptQueries &queries, int32_t id, const char *cls, bool subclasses, const int32_t *centre)
{
	Query *q = queries.Find(id);
	if (q == NULL)
		return 0;
	q->results.clear();
	const PClass *type = cls != NULL ? PClass::FindClass(cls) : NULL;
	Run run(*q);
	if (!IsActorClass(type) || !run.Resolve())
		return 0;
	const double from[3] = { centre ? centre[0] / 65536.0 : 0, centre ? centre[1] / 65536.0 : 0, centre ? centre[2] / 65536.0 : 0 };
	TThinkerIterator<AActor> it(type);
	std::vector<Candidate> found;
	while (AActor *actor = it.Next())
	{
		// [Doruk-Aksoy] Carried inventory isn't on the map.
		if ((actor->ObjectFlags & OF_EuthanizeMe) || !OfClass(actor, type, subclasses) ||
			(actor->IsKindOf(RUNTIME_CLASS(AInventory)) && static_cast<AInventory *>(actor)->Owner != NULL) || !run.Matches(actor))
			continue;
		found.push_back(Candidate{ actor, centre ? DistanceOf(actor, from, run.shape) : 0.0, found.size() });
	}
	return Keep(*q, found);
}

int32_t FoundActor(const ScriptQueries &queries, int32_t id, int index)
{
	const Query *q = queries.Find(id);
	if (q == NULL || index < 0 || size_t(index) >= q->results.size())
		return 0;
	const int32_t h = q->results[index];
	return acshandles::ActorOf(h) != NULL ? h : 0;
}

int FoundCount(const ScriptQueries &queries, int32_t id)
{
	const Query *q = queries.Find(id);
	return q != NULL ? int(q->results.size()) : 0;
}

void SerializeScript(FArchive &arc, ScriptQueries &queries)
{
	if (SaveVersion < kQuerySaveVersion)
	{
		if (arc.IsLoading())
			queries.queries.clear();
		return;
	}
	std::vector<int32_t> names;
	std::vector<int32_t> flat;
	if (arc.IsStoring())
	{
		// [Doruk-Aksoy] Names are saved as names, since name numbers change between runs.
		ScriptQueries saved = queries;
		ComputeRemapQueryNames(saved, [&](int32_t name) {
			const auto at = std::find(names.begin(), names.end(), name);
			if (at != names.end())
				return int32_t(at - names.begin() + 1);
			names.push_back(name);
			return int32_t(names.size());
		});
		flat = ComputeFlattenQueries(saved);
	}
	DWORD count = DWORD(names.size());
	arc << count;
	names.resize(count);
	for (int32_t &name : names)
	{
		FName n = ENamedName(name);
		arc << n;
		name = n.GetIndex();
	}
	count = DWORD(flat.size());
	arc << count;
	flat.resize(count);
	for (int32_t &v : flat)
	{
		SDWORD word = v;
		arc << word;
		v = word;
	}
	if (arc.IsStoring())
		return;
	if (!ComputeUnflattenQueries(flat.data(), flat.size(), queries))
	{
		Printf(TEXTCOLOR_ORANGE "ACS queries: a script's saved queries are damaged, so it starts with none.\n");
		queries.queries.clear();
		return;
	}
	ComputeRemapQueryNames(queries, [&](int32_t index) { return index >= 1 && size_t(index) <= names.size() ? names[index - 1] : 0; });
	g_lastId = std::max(g_lastId, ComputeHighestId(queries));
}

void WriteIds(FILE *file)
{
	const uint32_t id = uint32_t(g_lastId);
	const BYTE bytes[4] = { BYTE(id), BYTE(id >> 8), BYTE(id >> 16), BYTE(id >> 24) };
	M_AppendPNGChunk(file, kChunkId, bytes, 4);
}

void ReadIds(PNGHandle *png)
{
	BYTE bytes[4];
	if (M_FindPNGChunk(png, kChunkId) == 4 && fread(bytes, 1, 4, png->File->GetFile()) == 4)
		g_lastId = std::max(g_lastId, int32_t(uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24)));
	png->File->ResetFilePtr();
}

void ResetIds()
{
	g_lastId = 0;
}

void EnterLevel()
{
	g_warnedFull = false;
}

}} // namespace zx::acsqueries
