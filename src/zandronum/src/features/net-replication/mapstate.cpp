// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-replication/mapstate.h"

#include "c_cvars.h"
#include "c_dispatch.h"
#include "doomdef.h"
#include "doomstat.h"
#include "r_data/colormaps.h"
#include "r_defs.h"
#include "r_state.h"
#include "sv_commands.h"
#include "v_palette.h"

#include "features/net-protocol/lineflags.h"
#include "features/net-replication/actorstate.h"
#include "features/net-replication/computation/mapstate_compute.h"
#include "features/net-session/netrole.h"
#include "features/telemetry/telemetry.h"

#include <vector>

// [rc4l] Prints each change the audit found that no Touch had marked, naming the row and its families.
CVAR(Bool, fua_net_replication_misses, false, 0)

namespace {

using namespace zx::netreplication;

// [rc4l] Since the level's start: rows sent, rows the audit found changed untouched, and the first of those.
long g_totalSent = 0, g_totalMissed = 0;
FString g_firstMiss;

uint32_t Rgb(PalEntry p) { return (uint32_t(p.r) << 16) | (uint32_t(p.g) << 8) | uint32_t(p.b); }

SectorLook ReadSector(int i)
{
	const sector_t &s = sectors[i];
	SectorLook look;
	look.floorPic = s.GetTexture(sector_t::floor).GetIndex();
	look.ceilingPic = s.GetTexture(sector_t::ceiling).GetIndex();
	look.color = Rgb(s.ColorMap->Color);
	look.desaturate = s.ColorMap->Desaturate;
	look.fade = Rgb(s.ColorMap->Fade);
	look.gravity = s.gravity;
	look.friction = s.friction.Raw();
	look.moveFactor = s.movefactor.Raw();
	look.reflectFloor = s.reflect[sector_t::floor];
	look.reflectCeiling = s.reflect[sector_t::ceiling];
	look.xScaleFloor = s.GetXScale(sector_t::floor).Raw();
	look.yScaleFloor = s.GetYScale(sector_t::floor).Raw();
	look.xScaleCeiling = s.GetXScale(sector_t::ceiling).Raw();
	look.yScaleCeiling = s.GetYScale(sector_t::ceiling).Raw();
	look.angleFloor = s.GetAngle(sector_t::floor, false);
	look.angleCeiling = s.GetAngle(sector_t::ceiling, false);
	look.special = s.special;
	return look;
}

LineLook ReadLine(int i)
{
	const line_t &l = lines[i];
	LineLook look;
	look.alpha = l.Alpha.Raw();
	look.clientFlags = l.flags & zx::netprotocol::kClientLineFlags;
	for (int side = 0; side < 2; side++)
		for (int part = side_t::top; part <= side_t::bottom; part++)
			look.textures[side * 3 + part] = l.sidedef[side] != NULL ? l.sidedef[side]->GetTexture(part).GetIndex() : -1;
	return look;
}

uint32_t ReadSide(int i) { return sides[i].Flags; }
unsigned SideChanges(const uint32_t &was, const uint32_t &now) { return was != now ? 1u : 0u; }
uint32_t TakeSide(const uint32_t &, const uint32_t &now, unsigned) { return now; }
SectorLook TakeSector(const SectorLook &sent, const SectorLook &now, unsigned families) { return ComputeTakeFamilies(sent, now, families); }
LineLook TakeLine(const LineLook &sent, const LineLook &now, unsigned families) { return ComputeTakeFamilies(sent, now, families); }

void SendSector(int i, unsigned bits, ULONG extra, ServerCommandFlags flags)
{
	if (bits & kSectorFlat) SERVERCOMMANDS_SetSectorFlat(i, extra, flags);
	if (bits & kSectorColor) SERVERCOMMANDS_SetSectorColor(i, extra, flags);
	if (bits & kSectorFade) SERVERCOMMANDS_SetSectorFade(i, extra, flags);
	if (bits & kSectorGravity) SERVERCOMMANDS_SetSectorGravity(i, extra, flags);
	if (bits & kSectorFriction) SERVERCOMMANDS_SetSectorFriction(i, extra, flags);
	if (bits & kSectorReflection) SERVERCOMMANDS_SetSectorReflection(i, extra, flags);
	if (bits & kSectorScale) SERVERCOMMANDS_SetSectorScale(i, extra, flags);
	if (bits & kSectorRotation) SERVERCOMMANDS_SetSectorRotation(i, extra, flags);
	if (bits & kSectorSpecial) SERVERCOMMANDS_SetSectorSpecial(i, extra, flags);
}

void SendLine(int i, unsigned bits, ULONG extra, ServerCommandFlags flags)
{
	if (bits & kLineAlpha) SERVERCOMMANDS_SetLineAlpha(i, extra, flags);
	if (bits & kLineFlags) SERVERCOMMANDS_SetSomeLineFlags(i, extra, flags);
	if (const unsigned parts = ComputeLineTextureParts(bits)) SERVERCOMMANDS_SetLineTextureParts(i, parts, extra, flags);
}

void SendSide(int i, unsigned, ULONG extra, ServerCommandFlags flags) { SERVERCOMMANDS_SetSideFlags(i, extra, flags); }

// [rc4l] One kind of row (sectors, lines or sides): its look at the level's start and as last sent, the rows touched this tic, and the audit's place.
template <typename Look>
struct Rows
{
	const char *kind;
	int (*count)();
	Look (*read)(int);
	unsigned (*changes)(const Look &, const Look &);
	void (*send)(int, unsigned, ULONG, ServerCommandFlags);
	Look (*take)(const Look &, const Look &, unsigned);
	std::vector<Look> start, sent;
	std::vector<int> touched;
	std::vector<char> listed;
	int audit = 0;

	void Begin()
	{
		const int n = count();
		start.resize(n);
		for (int i = 0; i < n; i++) start[i] = read(i);
		sent = start;
		touched.clear();
		listed.assign(n, 0);
		audit = 0;
	}

	bool Ready() const { return (int)start.size() == count(); }

	void Touch(int i)
	{
		if (i < 0 || i >= (int)listed.size() || listed[i]) return;
		listed[i] = 1;
		touched.push_back(i);
	}

	void NoteSent(int i, unsigned families)
	{
		if (i >= 0 && i < (int)sent.size()) sent[i] = take(sent[i], read(i), families);
	}

	// [rc4l] Sends whatever of row i changed since it was last sent, to every client, and returns its families.
	unsigned Flush(int i)
	{
		const Look now = read(i);
		const unsigned bits = changes(sent[i], now);
		if (bits != 0)
		{
			send(i, bits, MAXPLAYERS, 0);
			sent[i] = now;
		}
		return bits;
	}

	void SendTouched()
	{
		int rows = 0;
		for (const int i : touched)
		{
			listed[i] = 0;
			rows += Flush(i) != 0;
		}
		touched.clear();
		g_totalSent += rows;
		ZX_COUNT("net/replication_sent", "rows", rows);
	}

	// [rc4l] The next slice of rows, every row once a second, any change found being one no Touch marked.
	void Audit()
	{
		const int n = (int)sent.size();
		if (n == 0) return;
		int missed = 0;
		for (int k = (n + TICRATE - 1) / TICRATE; k > 0; k--)
		{
			const int i = audit;
			audit = (audit + 1) % n;
			if (const unsigned bits = Flush(i))
			{
				missed++;
				if (g_totalMissed++ == 0) g_firstMiss.Format("%s %d (families %x)", kind, i, bits);
				if (fua_net_replication_misses) Printf("[replication] %s %d changed untouched (families %x)\n", kind, i, bits);
			}
		}
		ZX_COUNT("net/replication_missed", "rows", missed);
	}

	void Joiner(ULONG client)
	{
		for (int i = 0; i < (int)start.size(); i++)
			if (const unsigned bits = changes(start[i], read(i)))
				send(i, bits, client, SVCF_ONLYTHISCLIENT);
	}
};

int SectorCount() { return numsectors; }
int LineCount() { return numlines; }
int SideCount() { return numsides; }

Rows<SectorLook> g_sectors = { "sector", SectorCount, ReadSector, ComputeSectorChanges, SendSector, TakeSector };
Rows<LineLook> g_lines = { "line", LineCount, ReadLine, ComputeLineChanges, SendLine, TakeLine };
Rows<uint32_t> g_sides = { "side", SideCount, ReadSide, SideChanges, SendSide, TakeSide };

template <typename Fn> void EachKind(Fn fn) { fn(g_sectors); fn(g_lines); fn(g_sides); }

} // namespace

namespace zx { namespace netreplication {

void BeginLevel()
{
	if (!zx::net::IsServer()) return;
	EachKind([](auto &rows) { rows.Begin(); });
	g_totalSent = g_totalMissed = 0;
	g_firstMiss = "";
}

void TouchSector(int index) { g_sectors.Touch(index); }
void TouchLine(int index) { g_lines.Touch(index); }
void TouchSide(int index) { g_sides.Touch(index); }
void NoteSentSector(int index, unsigned families) { g_sectors.NoteSent(index, families); }
void NoteSentLine(int index, unsigned families) { g_lines.NoteSent(index, families); }

void SendChanges()
{
	if (!zx::net::IsServer()) return;
	SendWoundedHealth();
	EachKind([](auto &rows) {
		if (!rows.Ready()) rows.Begin();
		rows.SendTouched();
		rows.Audit();
	});
}

void SendToJoiner(unsigned client)
{
	EachKind([client](auto &rows) {
		if (rows.Ready()) rows.Joiner(client);
	});
}

}} // namespace zx::netreplication

// [rc4l] What the change set sent since the level started, and what it sent late because no Touch marked it.
CCMD(fua_net_replication)
{
	if (!zx::net::IsServer()) { Printf("fua_net_replication: only a server sends map state\n"); return; }
	Printf("replication: %ld rows sent, %ld missed%s%s\n", g_totalSent, g_totalMissed, g_totalMissed ? ", the first " : "", g_firstMiss.GetChars());
}
