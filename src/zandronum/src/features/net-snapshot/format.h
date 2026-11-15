//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: format.h
//
// Description: The snapshot wire format: quantizers, entity and event
// records, mover records and the .zsnap framing (design doc section 3,
// appendix B). The server's encoder (capture.cpp) and the client's
// decoder (entities.cpp) both use these definitions; never duplicate one
// on either side, since separate encoder and decoder copies drift apart.
// fua_snap_selftest checks the round trips.
//
//-----------------------------------------------------------------------------

#ifndef ZX_NETSNAPSHOT_FORMAT_H
#define ZX_NETSNAPSHOT_FORMAT_H

#include "basictypes.h"		// fixed_t, WORD, DWORD, ...
#include "tables.h"			// angle_t

//*****************************************************************************
//	QUANTIZERS

// [Kaori-Chani] The quantizers below carry 16.16 through 64-bit arithmetic, so they hold for ForkUnderA's 48.16 fixed_t as they did for a 32-bit one.

// Id field widths, ahead of the ceilings sized from them; SNAP_IDGAP_MAXBYTES
// bounds the reader as well as the writer (ENTITY ID ENCODING).
#define SNAP_IDGAP_MAXBYTES		3
#define SNAP_REC_IDBYTES_RAW	2

//*****************************************************************************
//	THE MAP'S POSITION FRAME
//
// Positions cross relative to the map's origin in whole units, at a width per
// axis the server picks once per map and SnapJoin carries.

#define SNAP_FRAME_NARROW		2		// bytes an axis: 16-bit whole units
#define SNAP_FRAME_WIDE			3		// bytes an axis spanning past 65,535 units
#define SNAP_FRAME_WIDEST		SNAP_FRAME_WIDE		// what sizes a buffer
#define SNAP_FRAME_MARGIN_XY	2048	// units added around the extent across
#define SNAP_FRAME_MARGIN_Z		1024	// ... and in z

struct SnapFrame
{
	int				origin[3];		// whole units, so a field's rounding commutes with it
	BYTE			width[3];		// bytes an axis: SNAP_FRAME_NARROW or SNAP_FRAME_WIDE
	unsigned int*	pClamped;		// bumped per value clamped into the frame; NULL counts nothing
};

namespace zx { namespace netsnapshot {

// Whether a width read from a file or a command has an encoding in this build.
inline bool FrameWidthKnown(unsigned int width)
{
	return (width == SNAP_FRAME_NARROW) || (width == SNAP_FRAME_WIDE);
}

inline SnapFrame FrameNarrow()
{
	SnapFrame f;
	for (unsigned int k = 0; k < 3; ++k)
	{
		f.origin[k] = 0;
		f.width[k] = SNAP_FRAME_NARROW;
	}
	f.pClamped = NULL;
	return f;
}

// Every axis wide at origin 0, for a budget that must hold in any frame.
inline SnapFrame FrameWidest()
{
	SnapFrame f = FrameNarrow();
	for (unsigned int k = 0; k < 3; ++k)
		f.width[k] = SNAP_FRAME_WIDEST;
	return f;
}

// Bytes axis k adds to a field over its narrow form: 0, or 1 when wide.
inline unsigned int FrameExtra(const SnapFrame& f, unsigned int k)
{
	return (unsigned int)f.width[k] - SNAP_FRAME_NARROW;
}

// The frame's whole-unit bounds on axis k: a signed value of its width.
inline long long FrameLo(const SnapFrame& f, unsigned int k)
{
	return (long long)f.origin[k] - (1LL << (8 * f.width[k] - 1));
}
inline long long FrameHi(const SnapFrame& f, unsigned int k)
{
	return (long long)f.origin[k] + (1LL << (8 * f.width[k] - 1)) - 1;
}

// Whole units clamped into the frame, never wrapped, and counted when clamped.
inline SDWORD FrameClamp(const SnapFrame& f, unsigned int k, long long u)
{
	const long long lo = FrameLo(f, k), hi = FrameHi(f, k);
	if ((u >= lo) && (u <= hi))
		return (SDWORD)u;
	if (f.pClamped != NULL)
		++*f.pClamped;
	return (SDWORD)((u < lo) ? lo : hi);
}

// The frame for a map's extent (whole units) within the engine's range
// [unitsMin, unitsMax]; `pOverflow` is set where an axis outgrows a wide frame.
inline SnapFrame FrameFromExtent(const long long lo[3], const long long hi[3],
	long long unitsMin, long long unitsMax, bool* pOverflow)
{
	SnapFrame f = FrameNarrow();
	if (pOverflow != NULL)
		*pOverflow = false;
	for (unsigned int k = 0; k < 3; ++k)
	{
		if (lo[k] > hi[k])
			continue;					// nothing on this axis: the narrow frame
		const long long margin = (k == 2) ? SNAP_FRAME_MARGIN_Z : SNAP_FRAME_MARGIN_XY;
		long long l = lo[k] - margin, h = hi[k] + margin;
		l = (l < unitsMin) ? unitsMin : l;
		h = (h > unitsMax) ? unitsMax : h;
		if ((l >= -32768) && (h <= 32767))
			continue;
		const long long span = h - l;
		// [Kaori-Chani] The origin is a 32-bit int in SnapFrame and on the wire, so a map centred past its range clamps here rather than truncating and misplacing every position on it.
		const long long centre = l + ((span + 1) >> 1);
		const long long originMin = -2147483647LL - 1, originMax = 2147483647LL;
		f.origin[k] = (int)((centre < originMin) ? originMin : (centre > originMax) ? originMax : centre);
		if (span > 0xFFFF)
			f.width[k] = SNAP_FRAME_WIDE;
		if ((span > 0xFFFFFF) && (pOverflow != NULL))
			*pOverflow = true;
	}
	return f;
}

// Position: a unit a step, clamped into the frame and rounded to nearest, so a
// mirror scatters about the true position rather than off to one side.
inline SDWORD QuantPosIn(const SnapFrame& f, unsigned int k, fixed_t v)
{
	// [Kaori-Chani] Rounded from the floor and the fraction, since adding half a unit to
	// a 48.16 value near its limit overflows where a 32-bit one could not.
	const long long raw = (long long)v;
	return FrameClamp(f, k, (raw >> 16) + (((raw & 0xFFFF) >= 0x8000) ? 1 : 0));
}

// The same, floored, for the fields that have always floored (`x >> FRACBITS`).
inline SDWORD FloorPosIn(const SnapFrame& f, unsigned int k, fixed_t v)
{
	return FrameClamp(f, k, (long long)v >> 16);
}

// Rounded into the narrow frame at origin 0: the frameless quantizer.
inline SDWORD QuantPos(fixed_t v)
{
	return QuantPosIn(FrameNarrow(), 0, v);
}
// [Kaori-Chani] Whole units to 16.16 as a 64-bit multiply, since a 48.16 position reaches past 32,767 units and shifting a negative value left is undefined.
inline fixed_t DequantPos(SDWORD q) { return (fixed_t)((long long)q * 65536); }

// Quantized position to whole map units for the server's distance tests, the
// identity kept so a precision change is one edit.
inline int PosToUnits(SDWORD q) { return (int)q; }

// Angle: 12 bits (steps of ~0.088 degrees).
inline WORD QuantAngle(angle_t a) { return (WORD)(a >> 20); }
inline angle_t DequantAngle(WORD q) { return ((angle_t)q) << 20; }

// Pitch: 10 bits.
inline WORD QuantPitch(fixed_t p) { return (WORD)(((angle_t)p) >> 22); }
// [Kaori-Chani] A pitch carries signed bits, so the field sign-extends into 48.16 rather than
// zero-extending and aiming a shot into the floor.
inline fixed_t DequantPitch(WORD q) { return fixed_t::FromSignedBits(((angle_t)q) << 22); }

// The pitch the client will hold, the top 8 of its 10 bits, which every held or
// compared pitch goes through, or ANG would be dirty every frame.
inline WORD WirePitch(WORD q) { return (WORD)(q & ~(WORD)3); }

// Velocity: 1/4 unit per step, clamped to a signed 14-bit range and rounded to
// nearest in 64 bits, so zero has a symmetric band.
inline SWORD QuantVel(fixed_t v)
{
	// [Kaori-Chani] Rounded from the floor and the fraction, as QuantPosIn is.
	const SQWORD raw = (SQWORD)v;
	SDWORD q = (SDWORD)((raw >> 14) + (((raw & 0x3FFF) >= 0x2000) ? 1 : 0));
	if (q > 8191) q = 8191;
	if (q < -8192) q = -8192;
	return (SWORD)q;
}
// [Kaori-Chani] A velocity step to 16.16 as a 64-bit multiply, for the same reason as DequantPos.
inline fixed_t DequantVel(SWORD q) { return (fixed_t)((long long)q * 16384); }

// Sprite+frame: sprite index (11 bits, clamped) | frame (5 bits).
inline WORD PackSpriteFrame(WORD sprite, BYTE frame)
{
	if (sprite > 2047) sprite = 2047;
	return (WORD)((sprite << 5) | (frame & 31));
}
inline WORD SpriteFromPack(WORD sf) { return (WORD)(sf >> 5); }
inline BYTE FrameFromPack(WORD sf) { return (BYTE)(sf & 31); }

}} // namespace zx::netsnapshot

//*****************************************************************************
//	RENDER STATE PACKING
//
// Layout: translation index (16) | legacy style index (5) | alpha (6) |
//         translation TYPE (5).

#define SNAP_RS_TRANS_MASK		0xFFFFu
#define SNAP_RS_STYLE_SHIFT		16
#define SNAP_RS_STYLE_MASK		0x1Fu
#define SNAP_RS_ALPHA_SHIFT		21
#define SNAP_RS_ALPHA_MASK		0x3Fu
#define SNAP_RS_TRANSTYPE_SHIFT	27
#define SNAP_RS_TRANSTYPE_MASK	0x1Fu
// Must equal the engine's TRANSLATION_SHIFT; asserted at the capture site.
#define SNAP_RS_TRANS_SHIFT		16

#define SNAP_FRACUNIT			65536

// qstate sentinel: state not owned by the actor's own class.
#define SNAP_STATE_UNKNOWN		0xFFFF

// qstate = owner depth (top 3 bits) | offset into that class's OwnedStates, the
// depth saying how far up the inheritance chain the owner is.
#define SNAP_STATE_DEPTH_SHIFT	13
#define SNAP_STATE_OFFSET_MASK	0x1FFF
// 6, not 7: depth 7 at the maximum offset would pack to the 0xFFFF sentinel.
#define SNAP_STATE_MAX_DEPTH	6


namespace zx { namespace netsnapshot {

// Alpha in 1/63rds, so opaque round-trips exactly (alpha >> 10 would leave
// every actor slightly translucent).
inline BYTE QuantAlpha(fixed_t alpha)
{
	if (alpha <= 0)
		return 0;
	if (alpha >= SNAP_FRACUNIT)
		return 63;
	return (BYTE)((((long long)alpha * 63) + (SNAP_FRACUNIT / 2)) / SNAP_FRACUNIT);
}

inline fixed_t DequantAlpha(BYTE a6)
{
	if (a6 >= 63)
		return SNAP_FRACUNIT;
	return (fixed_t)((((long long)a6 * SNAP_FRACUNIT) + 31) / 63);
}

inline DWORD PackRenderState(DWORD translation, BYTE styleIndex, fixed_t alpha)
{
	return (translation & SNAP_RS_TRANS_MASK)
		| (((translation >> SNAP_RS_TRANS_SHIFT) & SNAP_RS_TRANSTYPE_MASK)
			<< SNAP_RS_TRANSTYPE_SHIFT)
		| (((DWORD)styleIndex & SNAP_RS_STYLE_MASK) << SNAP_RS_STYLE_SHIFT)
		| (((DWORD)QuantAlpha(alpha) & SNAP_RS_ALPHA_MASK) << SNAP_RS_ALPHA_SHIFT);
}

inline DWORD TranslationFromState(DWORD rs)
{
	return (((rs >> SNAP_RS_TRANSTYPE_SHIFT) & SNAP_RS_TRANSTYPE_MASK)
			<< SNAP_RS_TRANS_SHIFT)
		| (rs & SNAP_RS_TRANS_MASK);
}
inline BYTE StyleIndexFromState(DWORD rs) { return (BYTE)((rs >> SNAP_RS_STYLE_SHIFT) & SNAP_RS_STYLE_MASK); }
inline fixed_t AlphaFromState(DWORD rs) { return DequantAlpha((BYTE)((rs >> SNAP_RS_ALPHA_SHIFT) & SNAP_RS_ALPHA_MASK)); }

}} // namespace zx::netsnapshot

//*****************************************************************************
//	ACTOR EFFECTS
//
// AActor::effects in a byte, as a fountain sets its own in PostBeginPlay, which
// mirrors skip; capture.cpp asserts the bits against p_effect.h's.
#define SNAP_FX_ROCKET			0x00000001
#define SNAP_FX_GRENADE			0x00000002
#define SNAP_FX_RESPAWNINVUL	0x00000020
#define SNAP_FX_VISIBILITYPULSE	0x00000040
#define SNAP_FX_VISIBILITYFLICKER 0x00000080
#define SNAP_FX_FOUNTAINMASK	0x00070000
#define SNAP_FX_FOUNTAINSHIFT	16

namespace zx { namespace netsnapshot {

inline BYTE PackEffects(DWORD effects)
{
	BYTE b = 0;
	if (effects & SNAP_FX_ROCKET)			b |= 1 << 0;
	if (effects & SNAP_FX_GRENADE)			b |= 1 << 1;
	if (effects & SNAP_FX_RESPAWNINVUL)		b |= 1 << 2;
	if (effects & SNAP_FX_VISIBILITYPULSE)	b |= 1 << 3;
	if (effects & SNAP_FX_VISIBILITYFLICKER) b |= 1 << 4;
	b |= (BYTE)(((effects & SNAP_FX_FOUNTAINMASK) >> SNAP_FX_FOUNTAINSHIFT) << 5);
	
	return b;
}

inline DWORD UnpackEffects(BYTE b)
{
	DWORD effects = 0;
	if (b & (1 << 0))	effects |= SNAP_FX_ROCKET;
	if (b & (1 << 1))	effects |= SNAP_FX_GRENADE;
	if (b & (1 << 2))	effects |= SNAP_FX_RESPAWNINVUL;
	if (b & (1 << 3))	effects |= SNAP_FX_VISIBILITYPULSE;
	if (b & (1 << 4))	effects |= SNAP_FX_VISIBILITYFLICKER;
	effects |= ((DWORD)(b >> 5) << SNAP_FX_FOUNTAINSHIFT) & SNAP_FX_FOUNTAINMASK;

	return effects;
}

}} // namespace zx::netsnapshot

//*****************************************************************************
//	ENTITY RECORD LAYOUT

// Per-group change bits for the delta mask.
enum
{
	SNAPGRP_POS = 1 << 0,	// qx, qy						(4 bytes)
	SNAPGRP_ANG = 1 << 1,	// qangle, qpitch				(3 bytes)
	SNAPGRP_POSZ = 1 << 2,	// qz							(2 bytes)
	SNAPGRP_VELMISC = 1 << 4,	// qvx, qvy, qvz, misc (16), effects	(9 bytes)
	// misc is the SNAPMISC_ and SNAP_MISC_ bits below, with bit 7 spare.
	SNAPGRP_APPEAR = 1 << 3,	// tics, state							(2 or 4 bytes)

	// Translation, alpha and style, kept from spawn to death by most actors, so
	// apart from APPEAR, on a mask bit so lengths follow from the mask.
	SNAPGRP_RENDER = 1 << 7,

	SNAPGRP_ALL = SNAPGRP_POS | SNAPGRP_POSZ | SNAPGRP_ANG | SNAPGRP_VELMISC
		| SNAPGRP_APPEAR | SNAPGRP_RENDER,
	SNAPGRP_GROUPMASK = SNAPGRP_ALL,

	// A classId follows: enter is declared rather than inferred from presence,
	// so a disagreement about presence is reported where it happens.
	SNAPGRP_ENTER = 1 << 5,

	// An event rather than state, tested FIRST, since the type's bits overlap
	// the group flags.
	SNAPGRP_EVENT = 1 << 6,
	SNAPEV_TYPEMASK = 0x1F,		// low 5 bits carry the type when EVENT is set

};

// How many state groups exist, which per-group reports size from; the assertion
// below fails the build when a group is added without it.
#define SNAP_GROUP_COUNT		6

#define SNAP_BITCOUNT8(m)	( ((m) & 1) + (((m) >> 1) & 1) + (((m) >> 2) & 1) \
	+ (((m) >> 3) & 1) + (((m) >> 4) & 1) + (((m) >> 5) & 1) \
	+ (((m) >> 6) & 1) + (((m) >> 7) & 1) )

typedef char snap_assert_group_count[
	(SNAP_BITCOUNT8(SNAPGRP_ALL) == SNAP_GROUP_COUNT) ? 1 : -1];

// Event types, in the low 5 bits of an EVENT mask.
enum
{
	// A branch off the animation chain: its game tic, state, phase and
	// position, since the brancher's last position is often stale.
	SNAPEV_BRANCH = 0,

	// A player's view sprites: state carried on the event escape, delta'd and
	// re-sent while unacked like a record.
	SNAPEV_PSPRITE = 1,

	// The local player's authoritative block for cl_pred.cpp's replay, sent
	// whole and unquantized to the one client the delta is built for.
	SNAPEV_LOCAL = 2,

	// Types 0-15 have a fixed length, an unknown one stopping the parse, and
	// 16-31 carry a length byte, so a reader skips one it does not know.
	SNAPEV_SELFDESC = 0x10,

	// A sector or polyobject mover (MOVER RECORDS, below).
	SNAPEV_MOVER = SNAPEV_SELFDESC | 0,

	SNAPEV_MAX = 31
};

// A self-describing length is 1-255: EventBytes returns 0 for an
// unknown type, so a zero length would collide with that sentinel.
#define SNAP_EVLEN_MAX		255
typedef char snap_assert_selfdesc_range[
	((SNAPEV_SELFDESC == 0x10) && (SNAPEV_MAX == 0x1F)
	&& (SNAPEV_BRANCH < SNAPEV_SELFDESC) && (SNAPEV_PSPRITE < SNAPEV_SELFDESC)
	&& (SNAPEV_LOCAL < SNAPEV_SELFDESC)) ? 1 : -1];

// An event's header is its variable-width id plus the mask byte, so only the
// ceiling is a constant.
#define SNAP_EVENT_MASK_BYTES	1
#define SNAP_EVENT_HEADER_MAX	( SNAP_IDGAP_MAXBYTES + SNAP_EVENT_MASK_BYTES )

// Payload bytes per type in the narrow frame, for the reader and the emitter's
// buffers alike, a wide axis adding a byte per position (EventBytes).
#define SNAP_EVBYTES_BRANCH	11	// tic 2 + qstate 2 + qtics 1 + qx/qy/qz 6
#define SNAP_EVBYTES_PSPRITE	8	// weapon class 2 + 2 x (qstate 2 + qtics 1)
#define SNAP_EVBYTES_LOCAL	31	// flags 1 + clientTic 4 + x/y/z 12 + vel 12 + jumpTics 2
#define SNAP_EVBYTES_WIDE_EXTRA	3	// both positioned types: one byte an axis

// The mover envelope, here since SNAP_EVBYTES_MOVER sizes every event buffer,
// and BODY_MAX bounds any class's body (a polyobject's door and move is 66).
#define SNAPMV_BODY_MAX		128		// widest per-class body; asserted below
#define SNAPMV_HEADER_BYTES	5		// class 1 + flags 1 + index 2 + generation 1

// The length byte is part of what the record occupies, so it is counted here.
#define SNAP_EVBYTES_MOVER	( 1 + SNAPMV_HEADER_BYTES + SNAPMV_BODY_MAX )

// The largest payload any type carries, which sizes every event buffer; a
// larger new type goes here as well as in EventBytes.
#define SNAP_EVBYTES_MAX	SNAP_EVBYTES_MOVER
#define SNAP_MAX_EVENT_BYTES	(SNAP_EVENT_HEADER_MAX + SNAP_EVBYTES_MAX)

// A larger type left out of SNAP_EVBYTES_MAX would undersize every event
// buffer.
typedef char snap_assert_evbytes_max[
	((SNAP_EVBYTES_BRANCH + SNAP_EVBYTES_WIDE_EXTRA <= SNAP_EVBYTES_MAX)
	&& (SNAP_EVBYTES_PSPRITE <= SNAP_EVBYTES_MAX)
	&& (SNAP_EVBYTES_LOCAL + SNAP_EVBYTES_WIDE_EXTRA <= SNAP_EVBYTES_MAX)
	&& (SNAP_EVBYTES_MOVER <= SNAP_EVBYTES_MAX)) ? 1 : -1];

namespace zx { namespace netsnapshot {

// Whether a type carries its own length byte; readers test this, not the range.
inline bool EventSelfDescribing(BYTE type)
{
	return ((type & SNAPEV_TYPEMASK) & SNAPEV_SELFDESC) != 0;
}

// Whether this build reads a self-describing type rather than skipping it, so a
// new type is a line here and nowhere else.
inline bool EventSelfDescKnown(BYTE type)
{
	switch (type & SNAPEV_TYPEMASK)
	{
	case SNAPEV_MOVER:	return true;
	default:			return false;
	}
}

// Payload bytes for a fixed-length type in frame `f`, or 0 (never a length) for
// one this build does not know; test EventSelfDescribing first.
inline unsigned int EventBytes(const SnapFrame& f, BYTE type)
{
	const unsigned int extra = FrameExtra(f, 0) + FrameExtra(f, 1)
		+ FrameExtra(f, 2);
	switch (type & SNAPEV_TYPEMASK)
	{
	case SNAPEV_BRANCH:	return SNAP_EVBYTES_BRANCH + extra;
	case SNAPEV_PSPRITE:	return SNAP_EVBYTES_PSPRITE;
	case SNAPEV_LOCAL:	return SNAP_EVBYTES_LOCAL + extra;
	default:			return 0;
	}
}

}} // namespace zx::netsnapshot

// How far past the mask byte (`p`, with `avail` left) an event record reaches,
// and whether the parse can carry on (SNAPEVLEN_*, snap_format.md).
enum { SNAPEVLEN_OK = 0, SNAPEVLEN_UNKNOWN, SNAPEVLEN_BAD };

namespace zx { namespace netsnapshot {

inline int EventRecordBytes(const SnapFrame& f, BYTE type, const BYTE* p,
	unsigned int avail, unsigned int& n, bool& bSkip)
{
	bSkip = false;
	if (EventSelfDescribing(type))
	{
		if (avail < 1)
			return SNAPEVLEN_BAD;
		const unsigned int len = p[0];
		if ((len == 0) || ((1 + len) > avail))
			return SNAPEVLEN_BAD;
		n = 1 + len;
		bSkip = (EventSelfDescKnown(type) == false);
		return SNAPEVLEN_OK;
	}
	const unsigned int fixed = EventBytes(f, type);
	if (fixed == 0)
		return SNAPEVLEN_UNKNOWN;
	if (fixed > avail)
		return SNAPEVLEN_BAD;
	n = fixed;
	return SNAPEVLEN_OK;
}

}} // namespace zx::netsnapshot

// Group widths, in write order: POS, POSZ, ANG, VELMISC, APPEAR, RENDER, with
// POS and POSZ at their narrow-frame widths, a wide axis adding a byte.
#define SNAP_GRPB_POS		4
#define SNAP_GRPB_POSZ		2
#define SNAP_GRPB_ANG		3
#define SNAP_GRPB_VELMISC	9
// APPEAR, the one variable group, is narrow (2 bytes) or wide (4) by the top
// bit of its first byte (snap_format.md).
#define SNAP_GRPB_APPEAR_NARROW	2
#define SNAP_GRPB_APPEAR_WIDE	4
#define SNAP_APPEAR_WIDE_FLAG	0x80

// The widest APPEAR, which only GroupBytes, the sizing function, takes.
#define SNAP_GRPB_APPEAR	SNAP_GRPB_APPEAR_WIDE
#define SNAP_GRPB_RENDER	4

namespace zx { namespace netsnapshot {

// Bytes before APPEAR, which a reader needs before it can see APPEAR: a
// function of the mask and the map's frame, whatever APPEAR costs.
inline unsigned int GroupBytesBeforeAppear(const SnapFrame& f, BYTE mask)
{
	unsigned int n = 0;
	mask &= SNAPGRP_GROUPMASK;
	if (mask & SNAPGRP_POS)
		n += SNAP_GRPB_POS + FrameExtra(f, 0) + FrameExtra(f, 1);
	if (mask & SNAPGRP_POSZ)
		n += SNAP_GRPB_POSZ + FrameExtra(f, 2);
	if (mask & SNAPGRP_ANG)		n += SNAP_GRPB_ANG;
	if (mask & SNAPGRP_VELMISC)	n += SNAP_GRPB_VELMISC;
	return n;
}

// The widest a mask can be in frame `f`, the only length that sizes a buffer.
inline unsigned int GroupBytes(const SnapFrame& f, BYTE mask)
{
	unsigned int n = GroupBytesBeforeAppear(f, mask);
	mask &= SNAPGRP_GROUPMASK;
	if (mask & SNAPGRP_APPEAR)	n += SNAP_GRPB_APPEAR;
	if (mask & SNAPGRP_RENDER)	n += SNAP_GRPB_RENDER;
	return n;
}

}} // namespace zx::netsnapshot

// One entity's quantized state, captured from the live actor on the server and
// decoded into the ghost on the client.
struct SnapEntityState
{
	WORD	id;				// NetID
	WORD	classId;		// getActorNetworkIndex() - same id space the legacy spawn commands use
	WORD	spriteframe;	// not on the wire, so 0 on the client; for debug output
	WORD	qangle;
	WORD	qpitch;
	// Whole map units, absolute, clamped into the map's frame at capture
	// (QuantPosIn); the origin applies only on the wire.
	SDWORD	qx, qy, qz;
	SWORD	qvx, qvy, qvz;
	DWORD	renderstate;	// RENDER STATE PACKING, above

	// Tics remaining in the current state (255: unknown or infinite), the phase
	// a resync needs so the mirror does not run late for good.
	BYTE	qtics;

	// The current state packed as above, or SNAP_STATE_UNKNOWN, sent since a
	// sprite frame is ambiguous exactly at a branch.
	WORD	qstate;
	WORD	misc;

	// AActor::effects packed, riding with misc since both are rarely dirty.
	BYTE	effects;
};

// A 4-bit checksum of the entity's full post-decode state in misc bits 8-11,
// which it excludes, so a field the record did not send is checked too.
#define SNAP_MISC_CHECKSUM ((WORD)0x0F00)
#define SNAP_MISC_CHECKSUM_SHIFT	8
#define SNAP_MISC_CHECKED ((WORD)~SNAP_MISC_CHECKSUM)

namespace zx { namespace netsnapshot {

inline BYTE StateChecksum(const SnapEntityState & e)
{
	DWORD h = 0;
	h ^= (DWORD)(WORD)e.qx;			h = (h << 3) | (h >> 29);
	h ^= (DWORD)(WORD)e.qy;			h = (h << 3) | (h >> 29);
	h ^= (DWORD)(WORD)e.qz;			h = (h << 3) | (h >> 29);

	// A position past 16 bits mixes in its high half; a narrow one's high half
	// is its sign extension and is skipped, so narrow checksums are unchanged.
	const SDWORD qpos[3] = { e.qx, e.qy, e.qz };
	for (unsigned int k = 0; k < 3; ++k)
	{
		if (qpos[k] != (SDWORD)(SWORD)qpos[k])
		{
			h ^= (DWORD)qpos[k] >> 16;	h = (h << 3) | (h >> 29);
		}
	}
	h ^= (DWORD)e.qangle;			h = (h << 3) | (h >> 29);
	// Pitch as the client receives it: the low 2 bits are dropped in transit,
	// so hashing them makes every entity with real pitch diverge forever.
	h ^= (DWORD)WirePitch(e.qpitch);	h = (h << 3) | (h >> 29);
	h ^= (DWORD)(WORD)e.qvx;		h = (h << 3) | (h >> 29);
	h ^= (DWORD)(WORD)e.qvy;		h = (h << 3) | (h >> 29);
	h ^= (DWORD)(WORD)e.qvz;		h = (h << 3) | (h >> 29);

	// qtics is not hashed, since it may go stale while absent.
	h ^= (DWORD)e.qstate;			h = (h << 3) | (h >> 29);
	h ^= (DWORD)e.renderstate;		h = (h << 3) | (h >> 29);
	h ^= (DWORD)e.effects;			h = (h << 3) | (h >> 29);
	h ^= (DWORD)(e.misc & SNAP_MISC_CHECKED);

	// Fold to 4 bits so every byte of the hash contributes.
	h ^= (h >> 16);
	h ^= (h >> 8);
	h ^= (h >> 4);
	return (BYTE)(h & 0x0F);
}

}} // namespace zx::netsnapshot

#define SNAP_MAXID			65536

// The appearance-suppression control group: one id in SNAP_APPEAR_EXEMPT_STRIDE
// is never suppressed, so its records measure the suppressed ones (9.2).
#define SNAP_APPEAR_EXEMPT_STRIDE	32

namespace zx { namespace netsnapshot {

inline bool AppearExempt(WORD id)
{
	return (id % SNAP_APPEAR_EXEMPT_STRIDE) == 0;
}

// Where a state flown unaided is after `tics`, from its quantized values, one
// implementation so the client's sampler and the server's suppression agree.
// accelZ is in units per tic per tic.
inline void ExtrapBallistic(const SnapEntityState& s, double tics,
	double& x, double& y, double& z, double accelZ = 0.0)
{
	x = (double)DequantPos(s.qx) + (double)DequantVel(s.qvx) * tics;
	y = (double)DequantPos(s.qy) + (double)DequantVel(s.qvy) * tics;
	z = (double)DequantPos(s.qz) + (double)DequantVel(s.qvz) * tics
		+ 0.5 * accelZ * tics * tics;
}

// The gravity class on both ends: P_ZMovement's step (z += velz, then velz -=
// g, the reverse under ZACOMPATF_OLD_ZDOOM_ZMOVEMENT) from velz `vz0` at the
// record's tic, g positive down in fixed units per tic per tic.
inline void ExtrapLaw(const SnapEntityState& s, double vz0, double tics,
	double g, bool bOldZ, double& x, double& y, double& z)
{
	x = (double)DequantPos(s.qx) + (double)DequantVel(s.qvx) * tics;
	y = (double)DequantPos(s.qy) + (double)DequantVel(s.qvy) * tics;
	z = (double)DequantPos(s.qz) + vz0 * tics
		- 0.5 * g * tics * (bOldZ ? (tics + 1.0) : (tics - 1.0));
}

}} // namespace zx::netsnapshot

// How long (400 ms) the server may withhold a ballistic missile's position,
// which must stay below the client's cap, fua_csnap_missile_extrap.
#define SNAP_BALLISTIC_MAX_TICS	14

// Datagrams a missing one may trail the newest before it counts as lost, one
// window for the server's fold and repair and the client's checksum.
#define SNAP_REORDER_WINDOW		8

// misc bit 0, a corpse, which a mirror cannot derive since it never runs the
// A_NoBlocking that clears MF_SOLID.
#define SNAPMISC_CORPSE		(1 << 0)

// The rest of misc's low byte, named so no bit is handed out twice.
#define SNAPMISC_INVISIBLE	((WORD)(1 << 1))
#define SNAPMISC_FLOORCLIP	((WORD)(1 << 2))
#define SNAPMISC_WATERSHIFT	3					// two bits, 3-4
#define SNAPMISC_WATERMASK	((WORD)(3 << SNAPMISC_WATERSHIFT))

// misc bits 5 and 6, set in the capture pass: velocity has held (ballistic), or
// the same population turning (steering).
#define SNAP_MISC_BALLISTIC	((WORD)(1 << 5))
#define SNAP_MISC_STEERING	((WORD)(1 << 6))

namespace zx { namespace netsnapshot {

// Whether either missile bit is set; the two bits are a three-way class, read
// only through these predicates.
inline bool MissileFlagged(WORD misc)
{
	return (misc & (SNAP_MISC_BALLISTIC | SNAP_MISC_STEERING)) != 0;
}

// Whether the client may extrapolate it at all: true for both ballistic forms.
inline bool IsBallistic(WORD misc)
{
	return (misc & SNAP_MISC_BALLISTIC) != 0;
}

// Ballistic, and with a constant Z acceleration the client must add.
inline bool IsGravBallistic(WORD misc)
{
	return (misc & (SNAP_MISC_BALLISTIC | SNAP_MISC_STEERING))
		== (SNAP_MISC_BALLISTIC | SNAP_MISC_STEERING);
}

// Whether the server may withhold its position: straight flight only, since a
// gravity path on the client is an approximation.
inline bool IsSuppressible(WORD misc)
{
	return (misc & (SNAP_MISC_BALLISTIC | SNAP_MISC_STEERING)) == SNAP_MISC_BALLISTIC;
}

// Turning, or not yet proven: stepped.
inline bool IsSteering(WORD misc)
{
	return (misc & (SNAP_MISC_BALLISTIC | SNAP_MISC_STEERING)) == SNAP_MISC_STEERING;
}

}} // namespace zx::netsnapshot

// misc bits 12-15: a player's crouch from standing (0) to the floor (15), sent
// as the value, since clients integrating the direction diverged.
#define SNAPMISC_CROUCH_SHIFT	12
#define SNAPMISC_CROUCH_MASK	((WORD)(0xF << SNAPMISC_CROUCH_SHIFT))

namespace zx { namespace netsnapshot {

// crouchfactor to and from the 4-bit value, FRACUNIT passed in since this
// header includes no engine header.
inline WORD QuantCrouch(int crouchfactor, int fracUnit)
{
	const int half = fracUnit / 2;
	if (crouchfactor >= fracUnit)
		return 0;
	if (crouchfactor <= half)
		return 0xF;
	// Round to nearest across 15 intervals, so a value one step off the floor
	// cannot read as the floor.
	const int span = fracUnit - half;
	const int q = ((fracUnit - crouchfactor) * 15 + (span / 2)) / span;
	return (WORD)((q < 0) ? 0 : ((q > 0xF) ? 0xF : q));
}

inline int CrouchFromWire(WORD q, int fracUnit)
{
	const int half = fracUnit / 2;
	if (q == 0)
		return fracUnit;
	if (q >= 0xF)
		return half;
	return fracUnit - (((int)q * (fracUnit - half)) / 15);
}

// The one definition of what changed, against the previous capture or
// `believed`, covering exactly the fields StateChecksum hashes.
inline BYTE DiffMask(const SnapEntityState& a, const SnapEntityState& b)
{
	BYTE mask = 0;
	if (a.qx != b.qx || a.qy != b.qy)							mask |= SNAPGRP_POS;
	// Z apart: it is unchanged on most position records.
	if (a.qz != b.qz)											mask |= SNAPGRP_POSZ;
	// Pitch at wire precision: the reference holds it truncated by the codec
	// round trip, so comparing all ten bits dirties ANG forever.
	if (a.qangle != b.qangle
		|| WirePitch(a.qpitch) != WirePitch(b.qpitch))	mask |= SNAPGRP_ANG;
	if (a.qvx != b.qvx || a.qvy != b.qvy || a.qvz != b.qvz)		mask |= SNAPGRP_VELMISC;
	if (a.qstate != b.qstate)									mask |= SNAPGRP_APPEAR;
	// Its own group, so an animation record does not carry four unchanged bytes.
	if (a.renderstate != b.renderstate)							mask |= SNAPGRP_RENDER;
	if (a.misc != b.misc || a.effects != b.effects)				mask |= SNAPGRP_VELMISC;
	return mask;
}

}} // namespace zx::netsnapshot

//*****************************************************************************
//	RECORD CODEC
//
// One implementation of the record layout for every producer and consumer:
// the server's capture pass, the client's decoder and the offline drivers.

// The worst narrow-frame record: id 3, mask 1, classId 2, target 2, and the
// groups 26 (POS 4, POSZ 2, ANG 3, VELMISC 9, APPEAR 4, RENDER 4).
#define SNAP_MAX_RECORD_BYTES_NARROW	34
typedef char snap_assert_max_record[
	(SNAP_MAX_RECORD_BYTES_NARROW == (SNAP_IDGAP_MAXBYTES + 1 + 2 + 2 + 26)) ? 1 : -1];

// What sizes a record buffer: the narrow worst case with every axis wide, so
// it bounds every record in every frame.
#define SNAP_MAX_RECORD_BYTES	( SNAP_MAX_RECORD_BYTES_NARROW \
	+ 3 * (SNAP_FRAME_WIDEST - SNAP_FRAME_NARROW) )

namespace zx { namespace netsnapshot {

// The worst case in frame `f`, for a budget test: the buffer constant would
// stop a narrow map's budget short.
inline unsigned int RecordBytesMax(const SnapFrame& f)
{
	return SNAP_MAX_RECORD_BYTES_NARROW + FrameExtra(f, 0) + FrameExtra(f, 1)
		+ FrameExtra(f, 2);
}

}} // namespace zx::netsnapshot

// An enter's classId with this bit is followed by the shooter's 2-byte NetID,
// read from the record, never the class, and every reader masks it off.
#define SNAP_CLASS_TARGET	0x8000
#define SNAP_CLASS_MASK		0x7FFF

// The widest thing one emit descriptor holds: the per-client emitter keeps
// entity records and events in one list, and an event can be the longer.
#define SNAP_MAX_EMIT_BYTES	( (SNAP_MAX_RECORD_BYTES > SNAP_MAX_EVENT_BYTES) \
	? SNAP_MAX_RECORD_BYTES : SNAP_MAX_EVENT_BYTES )
typedef char snap_assert_emit_bytes[
	((SNAP_MAX_EMIT_BYTES >= SNAP_MAX_RECORD_BYTES)
	&& (SNAP_MAX_EMIT_BYTES >= SNAP_MAX_EVENT_BYTES)) ? 1 : -1];

namespace zx { namespace netsnapshot {

// --- little-endian primitives ---
inline WORD Read16(const BYTE* p) { return (WORD)(p[0] | (p[1] << 8)); }
inline DWORD Read32(const BYTE* p)
{
	return (DWORD)(p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD)p[3] << 24));
}
// [Kaori-Chani] An absolute 32-bit 16.16 field off the wire holds a signed value in its bits, so it
// sign-extends into a 48.16 fixed_t rather than zero-extending a negative into a huge positive.
inline fixed_t ReadFixed32(const BYTE* p) { return fixed_t::FromSignedBits(Read32(p)); }
inline void Put16(BYTE* p, WORD v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); }

inline void Put32(BYTE* p, DWORD v) { Put16(p, (WORD)v); Put16(p + 2, (WORD)(v >> 16)); }

// A signed value in `bytes` little-endian bytes (2 to 5), and back.
inline void PutSigned(BYTE* p, long long v, unsigned int bytes)
{
	for (unsigned int i = 0; i < bytes; ++i)
		p[i] = (BYTE)(v >> (8 * i));
}
inline long long ReadSigned(const BYTE* p, unsigned int bytes)
{
	unsigned long long u = 0;
	for (unsigned int i = 0; i < bytes; ++i)
		u |= (unsigned long long)p[i] << (8 * i);
	const unsigned int shift = 64 - 8 * bytes;
	return (long long)(u << shift) >> shift;
}

// A quantized position on axis k, already in the frame, written relative to the
// origin, signed in the axis's width.
inline unsigned int PosBytes(const SnapFrame& f, unsigned int k)
{
	return f.width[k];
}
// The sum and difference wrap through DWORD, since a hostile origin can sit
// anywhere in an int and overflow is undefined.
inline SDWORD PosRel(const SnapFrame& f, unsigned int k, SDWORD q)
{
	return (SDWORD)((DWORD)q - (DWORD)f.origin[k]);
}
inline SDWORD PosAbs(const SnapFrame& f, unsigned int k, SDWORD rel)
{
	return (SDWORD)((DWORD)rel + (DWORD)f.origin[k]);
}
inline unsigned int PutPosIn(BYTE* p, const SnapFrame& f, unsigned int k, SDWORD q)
{
	PutSigned(p, PosRel(f, k, q), PosBytes(f, k));
	return PosBytes(f, k);
}
inline SDWORD ReadPosIn(const BYTE* p, const SnapFrame& f, unsigned int k)
{
	return PosAbs(f, k, (SDWORD)ReadSigned(p, PosBytes(f, k)));
}

// A full-precision position on axis k, 16.16 relative to the frame's origin:
// the axis's width in whole units and two bytes of fraction, clamped into the
// frame and counted when clamped.
inline unsigned int FixedBytes(const SnapFrame& f, unsigned int k)
{
	return f.width[k] + 2;
}
// The origin as 16.16, multiplied rather than shifted, since a negative value
// shifted left is undefined.
inline long long OriginFixed(const SnapFrame& f, unsigned int k)
{
	return (long long)f.origin[k] * 65536;
}
inline unsigned int PutFixedIn(BYTE* p, const SnapFrame& f, unsigned int k, fixed_t v)
{
	const long long rel = (long long)v - OriginFixed(f, k);
	const long long hi = (1LL << (8 * FixedBytes(f, k) - 1)) - 1, lo = -hi - 1;
	if (((rel < lo) || (rel > hi)) && (f.pClamped != NULL))
		++*f.pClamped;
	PutSigned(p, (rel < lo) ? lo : (rel > hi) ? hi : rel, FixedBytes(f, k));
	return FixedBytes(f, k);
}
inline fixed_t ReadFixedIn(const BYTE* p, const SnapFrame& f, unsigned int k)
{
	return (fixed_t)(ReadSigned(p, FixedBytes(f, k)) + OriginFixed(f, k));
}

//*****************************************************************************
//	ENTITY ID ENCODING
//
// A record's id is a varint of its gap mod 2^16 from the previous record's, the
// first in a payload raw, so each datagram decodes alone.

// Bytes a gap costs, a function of the gap alone.
inline unsigned int IdGapBytes(unsigned int gap)
{
	if (gap < 0x80)		return 1;
	if (gap < 0x4000)	return 2;
	return 3;
}

inline unsigned int PutIdGap(BYTE* p, unsigned int gap)
{
	unsigned int n = 0;
	while (gap >= 0x80)
	{
		p[n++] = (BYTE)(gap | 0x80);
		gap >>= 7;
	}
	p[n++] = (BYTE)gap;
	return n;
}

// Bytes consumed, or 0 for a gap cut short by `avail` or longer than the three
// bytes a 16-bit gap needs.
inline unsigned int ReadIdGap(const BYTE* p, unsigned int avail,
	unsigned int& gap)
{
	unsigned int v = 0, shift = 0, n = 0;
	while ((n < avail) && (n < SNAP_IDGAP_MAXBYTES))
	{
		const BYTE b = p[n++];
		v |= ((unsigned int)(b & 0x7F)) << shift;
		if ((b & 0x80) == 0)
		{
			gap = v & 0xFFFF;
			return n;
		}
		shift += 7;
	}
	gap = 0;
	return 0;
}

}} // namespace zx::netsnapshot

// The previous record's id while one payload is written or read, advanced by
// each writer and reader itself.
struct SnapIdCursor
{
	WORD	prev;
	bool	first;
};

namespace zx { namespace netsnapshot {

inline void IdCursorReset(SnapIdCursor& c)
{
	c.prev = 0;
	c.first = true;
}

inline unsigned int RecordIdBytes(WORD id, const SnapIdCursor& c)
{
	return c.first ? SNAP_REC_IDBYTES_RAW
		: IdGapBytes((unsigned int)(WORD)(id - c.prev));
}

inline unsigned int WriteRecordId(BYTE* p, WORD id, SnapIdCursor& c)
{
	unsigned int n;
	if (c.first)
	{
		Put16(p, id);
		n = SNAP_REC_IDBYTES_RAW;
	}
	else
	{
		n = PutIdGap(p, (unsigned int)(WORD)(id - c.prev));
	}
	c.prev = id;
	c.first = false;
	return n;
}

// Bytes consumed, or 0 on a truncated or malformed id, which leaves the cursor
// as it was.
inline unsigned int ReadRecordId(const BYTE* p, unsigned int avail,
	WORD& id, SnapIdCursor& c)
{
	unsigned int n;
	if (c.first)
	{
		if (avail < SNAP_REC_IDBYTES_RAW)
			return 0;
		id = Read16(p);
		n = SNAP_REC_IDBYTES_RAW;
	}
	else
	{
		unsigned int gap = 0;
		n = ReadIdGap(p, avail, gap);
		if (n == 0)
			return 0;
		id = (WORD)(c.prev + gap);
	}
	c.prev = id;
	c.first = false;
	return n;
}

// Whether a state fits the narrow APPEAR form, both sentinels (qstate 0xFFFF,
// qtics 255) going wide.
inline bool AppearIsNarrow(const SnapEntityState& e)
{
	return ((e.qstate >> SNAP_STATE_DEPTH_SHIFT) == 0)
		&& ((e.qstate & SNAP_STATE_OFFSET_MASK) < 256)
		&& (e.qtics < 128);
}

inline unsigned int AppearBytes(const SnapEntityState& e)
{
	return AppearIsNarrow(e) ? SNAP_GRPB_APPEAR_NARROW
		: SNAP_GRPB_APPEAR_WIDE;
}

// What the writer will emit for this state, which is what a census adds up.
inline unsigned int GroupBytesFor(const SnapFrame& f, BYTE mask,
	const SnapEntityState& e)
{
	unsigned int n = GroupBytesBeforeAppear(f, mask);
	if (mask & SNAPGRP_APPEAR)
		n += AppearBytes(e);
	if (mask & SNAPGRP_RENDER)
		n += SNAP_GRPB_RENDER;
	return n;
}

// What a reader must consume, from the bytes in hand, or false for a record
// truncated within `avail`.
inline bool GroupBytesRead(const SnapFrame& f, BYTE mask, const BYTE* p,
	unsigned int avail, unsigned int& n)
{
	const unsigned int pre = GroupBytesBeforeAppear(f, mask);
	unsigned int total = pre;
	if (mask & SNAPGRP_APPEAR)
	{
		if ((pre + 1) > avail)
			return false;			// cannot even see APPEAR's first byte
		total += ((p[pre] & SNAP_APPEAR_WIDE_FLAG) != 0)
			? SNAP_GRPB_APPEAR_WIDE : SNAP_GRPB_APPEAR_NARROW;
	}
	if (mask & SNAPGRP_RENDER)
		total += SNAP_GRPB_RENDER;
	if (total > avail)
		return false;
	n = total;
	return true;
}

// Writes the groups in mask into a buffer of GroupBytes( f, mask ),
// returning the bytes written; `pHeld`, the state the receiver will hold, feeds
// only the checksum (NULL: `e` itself).
inline unsigned int WriteGroups(BYTE* p, const SnapFrame& f,
	const SnapEntityState& e, BYTE mask, const SnapEntityState* pHeld = NULL)
{
	unsigned int n = 0;
	if (mask & SNAPGRP_POS)
	{
		n += PutPosIn(p + n, f, 0, e.qx);
		n += PutPosIn(p + n, f, 1, e.qy);
	}
	if (mask & SNAPGRP_POSZ)
		n += PutPosIn(p + n, f, 2, e.qz);
	if (mask & SNAPGRP_ANG)
	{
		Put16(p + n, e.qangle); n += 2;
		p[n++] = (BYTE)(e.qpitch >> 2);	// top 8 of 10 pitch bits
	}
	if (mask & SNAPGRP_VELMISC)
	{
		Put16(p + n, (WORD)e.qvx); n += 2;
		Put16(p + n, (WORD)e.qvy); n += 2;
		Put16(p + n, (WORD)e.qvz); n += 2;
		
		// The checksum covers what the receiver will hold, which differs from
		// `e` when a group is withheld.
		const SnapEntityState& forSum = (pHeld != NULL) ? *pHeld : e;
		const WORD miscOut = (WORD)((e.misc & SNAP_MISC_CHECKED)
				| ((WORD)StateChecksum(forSum) << SNAP_MISC_CHECKSUM_SHIFT));
		Put16(p + n, miscOut); n += 2;
		p[n++] = e.effects;
	}
	if (mask & SNAPGRP_APPEAR)
	{
		if (AppearIsNarrow(e))
		{
			p[n++] = (BYTE)(e.qtics & 0x7F);	// top bit clear: narrow
			p[n++] = (BYTE)(e.qstate & 0xFF);	// offset; depth is 0 by the test
		}
		else
		{
			p[n++] = SNAP_APPEAR_WIDE_FLAG;
			p[n++] = e.qtics;
			Put16(p + n, e.qstate); n += 2;
		}
	}
	if (mask & SNAPGRP_RENDER)
	{
		Put32(p + n, e.renderstate); n += 4;
	}
	return n;
}

// Reads the groups in mask onto st, the caller having checked the bounds with
// GroupBytesRead, never GroupBytes.
inline void ReadGroups(const BYTE* p, const SnapFrame& f, SnapEntityState& st,
	BYTE mask)
{
	if (mask & SNAPGRP_POS)
	{
		st.qx = ReadPosIn(p, f, 0); p += PosBytes(f, 0);
		st.qy = ReadPosIn(p, f, 1); p += PosBytes(f, 1);
	}
	if (mask & SNAPGRP_POSZ)
	{
		st.qz = ReadPosIn(p, f, 2); p += PosBytes(f, 2);
	}
	if (mask & SNAPGRP_ANG)
	{
		st.qangle = Read16(p); p += 2;
		st.qpitch = (WORD)(p[0] << 2); p += 1;
	}
	if (mask & SNAPGRP_VELMISC)
	{
		st.qvx = (SWORD)Read16(p); p += 2;
		st.qvy = (SWORD)Read16(p); p += 2;
		st.qvz = (SWORD)Read16(p); p += 2;
		st.misc = Read16(p); p += 2;
		st.effects = p[0]; p += 1;
	}
	if (mask & SNAPGRP_APPEAR)
	{
		if ((p[0] & SNAP_APPEAR_WIDE_FLAG) == 0)
		{
			st.qtics = (BYTE)(p[0] & 0x7F);
			st.qstate = (WORD)p[1];			// depth 0, offset in the low byte
			p += SNAP_GRPB_APPEAR_NARROW;
		}
		else
		{
			st.qtics = p[1];
			st.qstate = Read16(p + 2);
			p += SNAP_GRPB_APPEAR_WIDE;
		}
	}
	if (mask & SNAPGRP_RENDER)
	{
		st.renderstate = Read32(p); p += 4;
	}
}

// Overlays the groups in `mask` from src onto dst through the codec rather than
// a fourth field list, leaving dst's pitch as the client will hold it.
inline void MergeGroups(SnapEntityState& dst, const SnapFrame& f,
	const SnapEntityState& src, BYTE mask)
{
	BYTE buf[SNAP_MAX_RECORD_BYTES];
	const unsigned int n = WriteGroups(buf, f, src, mask);
	(void)n;
	ReadGroups(buf, f, dst, mask);

	// The round trip stamps a checksum into misc, which a captured state never
	// holds, so it is cleared when VELMISC was merged.
	if (mask & SNAPGRP_VELMISC)
		dst.misc = (WORD)(dst.misc & SNAP_MISC_CHECKED);
}


// Writes one event's body, mask and payload (the caller writes the id), and
// returns its bytes; `tic` is the game tic the branch was observed on, so a
// late event cannot wind back an entity a newer record moved.
inline unsigned int WriteEventBody(BYTE* p, const SnapFrame& f, BYTE type,
	const SnapEntityState& e, unsigned int tic)
{
	p[0] = (BYTE)(SNAPGRP_EVENT | (type & SNAPEV_TYPEMASK));
	unsigned int n = SNAP_EVENT_MASK_BYTES;
	if ((type & SNAPEV_TYPEMASK) == SNAPEV_BRANCH)
	{
		Put16(p + n, (WORD)tic);	n += 2;
		Put16(p + n, e.qstate);	n += 2;
		p[n++] = e.qtics;
		n += PutPosIn(p + n, f, 0, e.qx);
		n += PutPosIn(p + n, f, 1, e.qy);
		n += PutPosIn(p + n, f, 2, e.qz);
	}
	return n;
}

}} // namespace zx::netsnapshot

// Two psprite layers as they cross the wire, `qtics` as in APPEAR.
struct SnapPSpriteState
{
	WORD	weaponClass;		// getActorNetworkIndex(), 0 = no weapon
	WORD	qstate[2];			// ps_weapon, ps_flash; SNAP_STATE_UNKNOWN if unset
	BYTE	qtics[2];
};

namespace zx { namespace netsnapshot {

// Body only; see WriteEventBody.
inline unsigned int WritePSpriteBody(BYTE* p,
	const SnapPSpriteState& s)
{
	p[0] = (BYTE)(SNAPGRP_EVENT | SNAPEV_PSPRITE);
	unsigned int n = SNAP_EVENT_MASK_BYTES;
	Put16(p + n, s.weaponClass);	n += 2;
	for (unsigned int L = 0; L < 2; ++L)
	{
		Put16(p + n, s.qstate[L]);	n += 2;
		p[n++] = s.qtics[L];
	}
	return n;
}

// Payload only; the caller has already consumed id and mask.
inline void ReadPSprite(const BYTE* p, SnapPSpriteState& s)
{
	s.weaponClass = Read16(p);	p += 2;
	for (unsigned int L = 0; L < 2; ++L)
	{
		s.qstate[L] = Read16(p);	p += 2;
		s.qtics[L] = p[0];				p += 1;
	}
}

}} // namespace zx::netsnapshot

// Set by the server from its suppression cvar, since the machine that gates
// MoveLocalPlayer must be the one that says so.
#define SNAPLOCAL_AUTHORITATIVE	( 1 << 0 )

// What cl_pred.cpp's replay takes as server truth; viewheight, on-ground,
// crouch and the weapon timeline are not carried (4.2).
struct SnapLocalState
{
	BYTE	flags;			// SNAPLOCAL_*
	DWORD	clientTic;		// the client gametic the server last processed
	fixed_t	x, y, z;		// unquantized
	fixed_t	velx, vely, velz;
	SWORD	jumpTics;		// signed: -1 and -2 are live values (p_user.cpp)
};

namespace zx { namespace netsnapshot {

// Body only; see WriteEventBody.
inline unsigned int WriteLocalBody(BYTE* p, const SnapFrame& f,
	const SnapLocalState& s)
{
	p[0] = (BYTE)(SNAPGRP_EVENT | SNAPEV_LOCAL);
	unsigned int n = SNAP_EVENT_MASK_BYTES;
	p[n] = s.flags;							n += 1;
	Put32(p + n, s.clientTic);			n += 4;
	n += PutFixedIn(p + n, f, 0, s.x);
	n += PutFixedIn(p + n, f, 1, s.y);
	n += PutFixedIn(p + n, f, 2, s.z);
	Put32(p + n, (DWORD)s.velx);		n += 4;
	Put32(p + n, (DWORD)s.vely);		n += 4;
	Put32(p + n, (DWORD)s.velz);		n += 4;
	Put16(p + n, (WORD)s.jumpTics);	n += 2;
	return n;
}

// Payload only; the caller has already consumed id and mask.
inline void ReadLocal(const BYTE* p, const SnapFrame& f, SnapLocalState& s)
{
	s.flags = p[0];							p += 1;
	s.clientTic = Read32(p);			p += 4;
	s.x = ReadFixedIn(p, f, 0);		p += FixedBytes(f, 0);
	s.y = ReadFixedIn(p, f, 1);		p += FixedBytes(f, 1);
	s.z = ReadFixedIn(p, f, 2);		p += FixedBytes(f, 2);
	s.velx = ReadFixed32(p);		p += 4;
	s.vely = ReadFixed32(p);		p += 4;
	s.velz = ReadFixed32(p);		p += 4;
	s.jumpTics = (SWORD)Read16(p);		p += 2;
}

}} // namespace zx::netsnapshot

// The writer and reader above must match the constant that sizes every
// buffer carrying them.
typedef char snap_assert_evbytes_local[
	(SNAP_EVBYTES_LOCAL == (1 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 2)) ? 1 : -1];

namespace zx { namespace netsnapshot {

// Reads a branch payload (id and mask consumed), returning the tic it was
// observed on, so the caller can reject one older than the state it overwrites.
inline WORD ReadEvent(const BYTE * p, const SnapFrame& f, BYTE type,
	SnapEntityState & e)
{
	WORD tic = 0;
	if ((type & SNAPEV_TYPEMASK) == SNAPEV_BRANCH)
	{
		tic = Read16(p);		p += 2;
		e.qstate = Read16(p);	p += 2;
		e.qtics = p[0];				p += 1;
		e.qx = ReadPosIn(p, f, 0);	p += PosBytes(f, 0);
		e.qy = ReadPosIn(p, f, 1);	p += PosBytes(f, 1);
		e.qz = ReadPosIn(p, f, 2);	p += PosBytes(f, 2);
	}
	return tic;
}

}} // namespace zx::netsnapshot

// The mask byte; the census counts the variable-width id separately.
#define SNAP_REC_MASK_BYTES	1

namespace zx { namespace netsnapshot {

// Writes a record body, mask, classId on an enter, then the groups: SNAPGRP_ALL
// with the classId enters, mask 0 is a leave, and a nonzero `targetId` on an
// enter follows the class.
inline unsigned int WriteRecordBody(BYTE* p, const SnapFrame& f, BYTE mask,
	WORD classId, const SnapEntityState& e, const SnapEntityState* pHeld = NULL,
	WORD targetId = 0)
{
	unsigned int n = 0;
	p[n++] = mask;
	if ((mask & SNAPGRP_GROUPMASK) == 0)
		return n;					// leave tombstone
	if (mask & SNAPGRP_ENTER)
	{
		Put16(p + n, (WORD)((classId & SNAP_CLASS_MASK)
			| ((targetId != 0) ? SNAP_CLASS_TARGET : 0))); n += 2;
		if (targetId != 0)
		{
			Put16(p + n, targetId); n += 2;
		}
	}
	n += WriteGroups(p + n, f, e, mask, pHeld);
	return n;
}

// A whole record, id included, for the producers that write a payload in one
// pass, unlike the per-client emitter, which writes ids at layout.
inline unsigned int WriteRecord(BYTE* p, const SnapFrame& f, WORD id,
	SnapIdCursor& c, BYTE mask, WORD classId, const SnapEntityState& e,
	const SnapEntityState* pHeld = NULL, WORD targetId = 0)
{
	unsigned int n = WriteRecordId(p, id, c);
	n += WriteRecordBody(p + n, f, mask, classId, e, pHeld, targetId);
	return n;
}

}} // namespace zx::netsnapshot

//*****************************************************************************
//	.ZSNAP FIXTURE FILE FRAMING (design doc appendix B; little-endian)
//
// The file's constants and versions are with ZSNAP_VERSION, after the movers.

//*****************************************************************************
//	MOVER RECORDS
//
// Sector and polyobject thinkers, captured rather than announced (7.1), with
// id 0 and the index in the body, so each costs a one-byte gap.

// Which thinker a body describes, the values appended, never reordered.
enum
{
	SNAPMV_PLAT = 0,
	SNAPMV_FLOOR,
	SNAPMV_ELEVATOR,
	SNAPMV_PILLAR,
	SNAPMV_CEILING,
	SNAPMV_DOOR,
	SNAPMV_ANIMDOOR,
	SNAPMV_POLY,		// one record per polyobject, every action on it
	SNAPMV_COUNT
};

namespace zx { namespace netsnapshot {

// The classes the snapshot path tracks, whose legacy commands a gating server
// stops (DAnimatedDoor is replicated by its effects).
inline bool MoverClassTracked(unsigned int cls)
{
	return (cls < SNAPMV_COUNT) && (cls != SNAPMV_ANIMDOOR);
}

}} // namespace zx::netsnapshot

// Flags byte, blocked replicated since a client running no collision for a
// mover cannot tell a stall from a reversal (7.1).
#define SNAPMVF_BLOCKED		0x01	// server's mover is stalled on an obstruction
#define SNAPMVF_GONE		0x02	// thinker destroyed; the client drops its own
#define SNAPMVF_UPDATE		0x04	// RESERVED: body is the mutable subset, not full
// 0x08 was SNAPMVF_POLY, retired in v31 as SNAPMV_POLY says it; never reused.
#define SNAPMVF_STAIR		0x10	// floor body carries the stair timers
// A GONE of a two-plane class no longer speaking for a plane a successor took
// (both set would be a retire, never emitted).
#define SNAPMVF_NOFLOOR		0x20
#define SNAPMVF_NOCEILING	0x40

namespace zx { namespace netsnapshot {

// The envelope's width for a stream version: class, flags, index and the
// generation (never 0), which a pre-v28 envelope lacks and reads as 0.
inline unsigned int MoverHeaderBytes(unsigned int version)
{
	return (version >= 28) ? SNAPMV_HEADER_BYTES : (SNAPMV_HEADER_BYTES - 1);
}

inline unsigned int WriteMoverHeader(BYTE* p, BYTE cls, BYTE flags,
	WORD index, BYTE gen)
{
	p[0] = cls;
	p[1] = flags;
	Put16(p + 2, index);
	p[4] = gen;
	return SNAPMV_HEADER_BYTES;
}

// `hdrBytes` is MoverHeaderBytes of the stream being read.
inline void ReadMoverHeader(const BYTE* p, unsigned int hdrBytes,
	BYTE& cls, BYTE& flags, WORD& index, BYTE& gen)
{
	cls = p[0];
	flags = p[1];
	index = Read16(p + 2);
	gen = (hdrBytes > 4) ? p[4] : 0;
}

}} // namespace zx::netsnapshot

// One plat: every field DPlat::Serialize carries, plus the plane, which no
// field of the thinker gives.
struct SnapMoverPlat
{
	fixed_t	speed, low, high, planeD;
	SWORD	wait, count, tag, platID, crush;
	BYTE	status, oldStatus, type;
};

#define SNAPMV_PLAT_BYTES	(4 + 4 + 4 + 4 + 2 + 2 + 2 + 2 + 2 + 1 + 1 + 1)

namespace zx { namespace netsnapshot {

inline unsigned int WriteMoverPlat(BYTE* p, const SnapMoverPlat& s)
{
	unsigned int n = 0;
	Put32(p + n, (DWORD)s.speed);	n += 4;
	Put32(p + n, (DWORD)s.low);	n += 4;
	Put32(p + n, (DWORD)s.high);	n += 4;
	Put32(p + n, (DWORD)s.planeD);	n += 4;
	Put16(p + n, (WORD)s.wait);	n += 2;
	Put16(p + n, (WORD)s.count);	n += 2;
	Put16(p + n, (WORD)s.tag);		n += 2;
	Put16(p + n, (WORD)s.platID);	n += 2;
	Put16(p + n, (WORD)s.crush);	n += 2;
	p[n++] = s.status;
	p[n++] = s.oldStatus;
	p[n++] = s.type;
	return n;
}

inline void ReadMoverPlat(const BYTE* p, SnapMoverPlat& s)
{
	unsigned int n = 0;
	s.speed  = ReadFixed32(p + n);	n += 4;
	s.low	 = ReadFixed32(p + n);	n += 4;
	s.high	 = ReadFixed32(p + n);	n += 4;
	s.planeD = ReadFixed32(p + n);	n += 4;
	s.wait	 = (SWORD)Read16(p + n);	n += 2;
	s.count	 = (SWORD)Read16(p + n);	n += 2;
	s.tag	 = (SWORD)Read16(p + n);	n += 2;
	s.platID = (SWORD)Read16(p + n);	n += 2;
	s.crush	 = (SWORD)Read16(p + n);	n += 2;
	s.status	= p[n++];
	s.oldStatus	= p[n++];
	s.type		= p[n++];
}

}} // namespace zx::netsnapshot

// One ceiling: DCeiling::Serialize's members less m_Texture and m_NewSpecial,
// read only at a terminal pastdest (snap_format.md).
struct SnapMoverCeiling
{
	fixed_t	speed, speed1, speed2, bottom, top, planeD;	// the ceiling plane
	SWORD	tag, ceilingID, crush;
	BYTE	type, silent, hexencrush;
	SBYTE	direction, oldDirection;	// -1 down, 0 stasis, 1 up
};

#define SNAPMV_CEILING_BYTES	(4 + 4 + 4 + 4 + 4 + 4 + 2 + 2 + 2 + 1 + 1 + 1 + 1 + 1)

namespace zx { namespace netsnapshot {

inline unsigned int WriteMoverCeiling(BYTE* p, const SnapMoverCeiling& s)
{
	unsigned int n = 0;
	Put32(p + n, (DWORD)s.speed);		n += 4;
	Put32(p + n, (DWORD)s.speed1);		n += 4;
	Put32(p + n, (DWORD)s.speed2);		n += 4;
	Put32(p + n, (DWORD)s.bottom);		n += 4;
	Put32(p + n, (DWORD)s.top);		n += 4;
	Put32(p + n, (DWORD)s.planeD);		n += 4;
	Put16(p + n, (WORD)s.tag);			n += 2;
	Put16(p + n, (WORD)s.ceilingID);	n += 2;
	Put16(p + n, (WORD)s.crush);		n += 2;
	p[n++] = s.type;
	p[n++] = s.silent;
	p[n++] = s.hexencrush;
	p[n++] = (BYTE)s.direction;
	p[n++] = (BYTE)s.oldDirection;
	return n;
}

inline void ReadMoverCeiling(const BYTE* p, SnapMoverCeiling& s)
{
	unsigned int n = 0;
	s.speed		= ReadFixed32(p + n);	n += 4;
	s.speed1	= ReadFixed32(p + n);	n += 4;
	s.speed2	= ReadFixed32(p + n);	n += 4;
	s.bottom	= ReadFixed32(p + n);	n += 4;
	s.top		= ReadFixed32(p + n);	n += 4;
	s.planeD	= ReadFixed32(p + n);	n += 4;
	s.tag		= (SWORD)Read16(p + n);	n += 2;
	s.ceilingID	= (SWORD)Read16(p + n);	n += 2;
	s.crush		= (SWORD)Read16(p + n);	n += 2;
	s.type			= p[n++];
	s.silent		= p[n++];
	s.hexencrush	= p[n++];
	s.direction		= (SBYTE)p[n++];
	s.oldDirection	= (SBYTE)p[n++];
}

}} // namespace zx::netsnapshot

// One floor: DFloor::Serialize's members less m_Texture and m_NewSpecial, as
// for the ceiling, SNAPMVF_STAIR selecting the shape with the stair fields.
struct SnapMoverFloor
{
	fixed_t	speed, destDist, planeD;
	SWORD	floorID, crush;
	BYTE	type, hexencrush;
	SBYTE	direction;

	// SNAPMVF_STAIR only; zeroed by the reader otherwise.
	fixed_t	orgDist;
	int		resetCount, delay, pauseTime, stepTime, perStepTime;
};

#define SNAPMV_FLOOR_BYTES		(4 + 4 + 4 + 2 + 2 + 1 + 1 + 1)
#define SNAPMV_FLOOR_STAIR_BYTES	(SNAPMV_FLOOR_BYTES + 4 + 4 + 4 + 4 + 4 + 4)

namespace zx { namespace netsnapshot {

inline unsigned int WriteMoverFloor(BYTE* p, const SnapMoverFloor& s,
	bool bStair)
{
	unsigned int n = 0;
	Put32(p + n, (DWORD)s.speed);		n += 4;
	Put32(p + n, (DWORD)s.destDist);	n += 4;
	Put32(p + n, (DWORD)s.planeD);		n += 4;
	Put16(p + n, (WORD)s.floorID);		n += 2;
	Put16(p + n, (WORD)s.crush);		n += 2;
	p[n++] = s.type;
	p[n++] = s.hexencrush;
	p[n++] = (BYTE)s.direction;
	if (bStair)
	{
		Put32(p + n, (DWORD)s.orgDist);		n += 4;
		Put32(p + n, (DWORD)s.resetCount);		n += 4;
		Put32(p + n, (DWORD)s.delay);			n += 4;
		Put32(p + n, (DWORD)s.pauseTime);		n += 4;
		Put32(p + n, (DWORD)s.stepTime);		n += 4;
		Put32(p + n, (DWORD)s.perStepTime);	n += 4;
	}
	return n;
}

inline void ReadMoverFloor(const BYTE* p, SnapMoverFloor& s, bool bStair)
{
	unsigned int n = 0;
	s.speed		= ReadFixed32(p + n);	n += 4;
	s.destDist	= ReadFixed32(p + n);	n += 4;
	s.planeD	= ReadFixed32(p + n);	n += 4;
	s.floorID	= (SWORD)Read16(p + n);	n += 2;
	s.crush		= (SWORD)Read16(p + n);	n += 2;
	s.type			= p[n++];
	s.hexencrush	= p[n++];
	s.direction		= (SBYTE)p[n++];
	if (bStair)
	{
		s.orgDist		= ReadFixed32(p + n);	n += 4;
		s.resetCount	= (int)Read32(p + n);		n += 4;
		s.delay			= (int)Read32(p + n);		n += 4;
		s.pauseTime		= (int)Read32(p + n);		n += 4;
		s.stepTime		= (int)Read32(p + n);		n += 4;
		s.perStepTime	= (int)Read32(p + n);		n += 4;
	}
	else
	{
		// Defined, since a decoded record carries only what the wire did.
		s.orgDist = 0;
		s.resetCount = s.delay = s.pauseTime = s.stepTime = s.perStepTime = 0;
	}
}

}} // namespace zx::netsnapshot

// One door: DDoor::Serialize's members plus the plane, m_BotSpot crossing as
// one of the sector's line ends (snap_movers.md).
struct SnapMoverDoor
{
	fixed_t	speed, topDist, botDist, oldFloorDist, planeD;
	SDWORD	topWait, topCountdown, botSpot;	// topCountdown 0 unless waiting
	SWORD	lightTag, doorID;
	BYTE	type;
	SBYTE	direction;	// -1 down, 0 waiting, 1 up, 2 doorRaiseIn5Mins's first wait
};

#define SNAPMV_DOOR_BYTES	(4 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 2 + 2 + 1 + 1)

namespace zx { namespace netsnapshot {

inline unsigned int WriteMoverDoor(BYTE* p, const SnapMoverDoor& s)
{
	unsigned int n = 0;
	Put32(p + n, (DWORD)s.speed);			n += 4;
	Put32(p + n, (DWORD)s.topDist);		n += 4;
	Put32(p + n, (DWORD)s.botDist);		n += 4;
	Put32(p + n, (DWORD)s.oldFloorDist);	n += 4;
	Put32(p + n, (DWORD)s.planeD);			n += 4;
	Put32(p + n, (DWORD)s.topWait);		n += 4;
	Put32(p + n, (DWORD)s.topCountdown);	n += 4;
	Put32(p + n, (DWORD)s.botSpot);		n += 4;
	Put16(p + n, (WORD)s.lightTag);		n += 2;
	Put16(p + n, (WORD)s.doorID);			n += 2;
	p[n++] = s.type;
	p[n++] = (BYTE)s.direction;
	return n;
}

inline void ReadMoverDoor(const BYTE* p, SnapMoverDoor& s)
{
	unsigned int n = 0;
	s.speed			= ReadFixed32(p + n);	n += 4;
	s.topDist		= ReadFixed32(p + n);	n += 4;
	s.botDist		= ReadFixed32(p + n);	n += 4;
	s.oldFloorDist	= ReadFixed32(p + n);	n += 4;
	s.planeD		= ReadFixed32(p + n);	n += 4;
	s.topWait		= (SDWORD)Read32(p + n);	n += 4;
	s.topCountdown	= (SDWORD)Read32(p + n);	n += 4;
	s.botSpot		= (SDWORD)Read32(p + n);	n += 4;
	s.lightTag		= (SWORD)Read16(p + n);	n += 2;
	s.doorID		= (SWORD)Read16(p + n);	n += 2;
	s.type			= p[n++];
	s.direction		= (SBYTE)p[n++];
}

}} // namespace zx::netsnapshot

// One elevator: DElevator::Serialize less its interpolations, plus both planes,
// which the GONE carries too.
struct SnapMoverElevator
{
	fixed_t	speed, floorDestDist, ceilingDestDist, floorPlaneD, ceilingPlaneD;
	SWORD	elevatorID;
	BYTE	type;
	SBYTE	direction;
};

#define SNAPMV_ELEVATOR_BYTES	(4 + 4 + 4 + 4 + 4 + 2 + 1 + 1)

namespace zx { namespace netsnapshot {

inline unsigned int WriteMoverElevator(BYTE* p, const SnapMoverElevator& s)
{
	unsigned int n = 0;
	Put32(p + n, (DWORD)s.speed);				n += 4;
	Put32(p + n, (DWORD)s.floorDestDist);		n += 4;
	Put32(p + n, (DWORD)s.ceilingDestDist);	n += 4;
	Put32(p + n, (DWORD)s.floorPlaneD);		n += 4;
	Put32(p + n, (DWORD)s.ceilingPlaneD);		n += 4;
	Put16(p + n, (WORD)s.elevatorID);			n += 2;
	p[n++] = s.type;
	p[n++] = (BYTE)s.direction;
	return n;
}

inline void ReadMoverElevator(const BYTE* p, SnapMoverElevator& s)
{
	unsigned int n = 0;
	s.speed				= ReadFixed32(p + n);	n += 4;
	s.floorDestDist		= ReadFixed32(p + n);	n += 4;
	s.ceilingDestDist	= ReadFixed32(p + n);	n += 4;
	s.floorPlaneD		= ReadFixed32(p + n);	n += 4;
	s.ceilingPlaneD		= ReadFixed32(p + n);	n += 4;
	s.elevatorID		= (SWORD)Read16(p + n);	n += 2;
	s.type				= p[n++];
	s.direction			= (SBYTE)p[n++];
}

}} // namespace zx::netsnapshot

// One pillar: DPillar::Serialize less its interpolations, plus both planes,
// with `blocked` saying which plane stalled (SNAPMV_PLANE_*).
struct SnapMoverPillar
{
	fixed_t	floorSpeed, ceilingSpeed, floorTarget, ceilingTarget;
	fixed_t	floorPlaneD, ceilingPlaneD;
	SWORD	pillarID, crush;
	BYTE	type, hexencrush, blocked;
};

#define SNAPMV_PILLAR_BYTES	(4 + 4 + 4 + 4 + 4 + 4 + 2 + 2 + 1 + 1 + 1)

namespace zx { namespace netsnapshot {

inline unsigned int WriteMoverPillar(BYTE* p, const SnapMoverPillar& s)
{
	unsigned int n = 0;
	Put32(p + n, (DWORD)s.floorSpeed);		n += 4;
	Put32(p + n, (DWORD)s.ceilingSpeed);	n += 4;
	Put32(p + n, (DWORD)s.floorTarget);	n += 4;
	Put32(p + n, (DWORD)s.ceilingTarget);	n += 4;
	Put32(p + n, (DWORD)s.floorPlaneD);	n += 4;
	Put32(p + n, (DWORD)s.ceilingPlaneD);	n += 4;
	Put16(p + n, (WORD)s.pillarID);		n += 2;
	Put16(p + n, (WORD)s.crush);			n += 2;
	p[n++] = s.type;
	p[n++] = s.hexencrush;
	p[n++] = s.blocked;
	return n;
}

inline void ReadMoverPillar(const BYTE* p, SnapMoverPillar& s)
{
	unsigned int n = 0;
	s.floorSpeed	= ReadFixed32(p + n);	n += 4;
	s.ceilingSpeed	= ReadFixed32(p + n);	n += 4;
	s.floorTarget	= ReadFixed32(p + n);	n += 4;
	s.ceilingTarget	= ReadFixed32(p + n);	n += 4;
	s.floorPlaneD	= ReadFixed32(p + n);	n += 4;
	s.ceilingPlaneD	= ReadFixed32(p + n);	n += 4;
	s.pillarID		= (SWORD)Read16(p + n);	n += 2;
	s.crush			= (SWORD)Read16(p + n);	n += 2;
	s.type			= p[n++];
	s.hexencrush	= p[n++];
	s.blocked		= p[n++];
}

}} // namespace zx::netsnapshot

// One polyobject: its StartSpot and angle, then every action on it in creation
// order (override specials run several at once), no entries being the GONE.
#define SNAPPO_ROTATE		0
#define SNAPPO_MOVE			1
#define SNAPPO_MOVETO		2
#define SNAPPO_SLIDE		3	// DPolyDoor, PODOOR_SLIDE
#define SNAPPO_SWING		4	// DPolyDoor, PODOOR_SWING
#define SNAPPO_TYPEMASK		0x07
#define SNAPPO_SPECIAL		0x08	// the action poly->specialdata names
#define SNAPPO_NOINTERP		0x40	// interpolation dropped at creation
#define SNAPPO_BLOCKED		0x80	// stalled on an obstruction

// DPolyAction::Serialize per class, less the interpolation and, for a door,
// DMovePoly's m_Angle, which a door never reads.
struct SnapPolyEntry
{
	BYTE	kind, gen;			// gen names the action, never 0
	int		speed, dist;
	SWORD	angle;				// MOVE: fine angle
	SWORD	direction;			// doors
	fixed_t	xSpeed, ySpeed;		// MOVE, MOVETO, doors
	fixed_t	xTarget, yTarget;	// MOVETO
	int		totalDist, tics, waitTics;	// doors
	BYTE	close;				// doors
};

// In the narrow frame: a wide axis adds a byte to the header's x or y and to
// a MOVETO's target on it (PolyHeaderBytes, PolyEntryBytes).
#define SNAPPO_HEADER_BYTES		(4 + 4 + 4 + 1)
#define SNAPPO_ENTRY_BYTES		(1 + 1 + 4 + 4)
#define SNAPPO_MOVE_BYTES		(SNAPPO_ENTRY_BYTES + 2 + 4 + 4)
#define SNAPPO_MOVETO_BYTES		(SNAPPO_ENTRY_BYTES + 4 + 4 + 4 + 4)
#define SNAPPO_DOOR_BYTES		(SNAPPO_ENTRY_BYTES + 4 + 4 + 2 + 4 + 4 + 4 + 1)
#define SNAPPO_MAXENTRIES		((SNAPMV_BODY_MAX - SNAPPO_HEADER_BYTES) / SNAPPO_ENTRY_BYTES)

struct SnapMoverPoly
{
	fixed_t			x, y;
	angle_t			angle;
	BYTE			count;
	SnapPolyEntry	e[SNAPPO_MAXENTRIES];
};

namespace zx { namespace netsnapshot {

// The header's bytes in frame `f`: x, y, angle and the count.
inline unsigned int PolyHeaderBytes(const SnapFrame& f)
{
	return SNAPPO_HEADER_BYTES + FrameExtra(f, 0) + FrameExtra(f, 1);
}

// One entry's bytes in frame `f`, or 0 for a kind this build does not know.
inline unsigned int PolyEntryBytes(const SnapFrame& f, BYTE kind)
{
	switch (kind & SNAPPO_TYPEMASK)
	{
	case SNAPPO_ROTATE:	return SNAPPO_ENTRY_BYTES;
	case SNAPPO_MOVE:	return SNAPPO_MOVE_BYTES;
	case SNAPPO_MOVETO:	return SNAPPO_MOVETO_BYTES + FrameExtra(f, 0) + FrameExtra(f, 1);
	case SNAPPO_SLIDE:
	case SNAPPO_SWING:	return SNAPPO_DOOR_BYTES;
	default:			return 0;
	}
}

// The body's length in frame `f`, walked, or 0 if it overruns `avail`, names an
// unknown kind or counts past SNAPPO_MAXENTRIES.
inline unsigned int MoverPolyBodyBytes(const SnapFrame& f, const BYTE* p,
	unsigned int avail)
{
	const unsigned int header = PolyHeaderBytes(f);
	if (avail < header)
		return 0;
	const unsigned int count = p[header - 1];
	if (count > SNAPPO_MAXENTRIES)
		return 0;
	unsigned int n = header;
	for (unsigned int i = 0; i < count; ++i)
	{
		if (n >= avail)
			return 0;
		const unsigned int b = PolyEntryBytes(f, p[n]);
		if ((b == 0) || (n + b > avail))
			return 0;
		n += b;
	}
	return n;
}

// The capture keeps this within SNAPMV_BODY_MAX in the widest frame.
inline unsigned int WriteMoverPoly(BYTE* p, const SnapFrame& f,
	const SnapMoverPoly& s)
{
	unsigned int n = 0;
	n += PutFixedIn(p + n, f, 0, s.x);
	n += PutFixedIn(p + n, f, 1, s.y);
	Put32(p + n, (DWORD)s.angle);	n += 4;
	p[n++] = s.count;
	for (unsigned int i = 0; i < s.count; ++i)
	{
		const SnapPolyEntry & e = s.e[i];
		p[n++] = e.kind;
		p[n++] = e.gen;
		Put32(p + n, (DWORD)e.speed);	n += 4;
		Put32(p + n, (DWORD)e.dist);	n += 4;
		switch (e.kind & SNAPPO_TYPEMASK)
		{
		case SNAPPO_MOVE:
			Put16(p + n, (WORD)e.angle);		n += 2;
			Put32(p + n, (DWORD)e.xSpeed);		n += 4;
			Put32(p + n, (DWORD)e.ySpeed);		n += 4;
			break;
		case SNAPPO_MOVETO:
			Put32(p + n, (DWORD)e.xSpeed);		n += 4;
			Put32(p + n, (DWORD)e.ySpeed);		n += 4;
			n += PutFixedIn(p + n, f, 0, e.xTarget);
			n += PutFixedIn(p + n, f, 1, e.yTarget);
			break;
		case SNAPPO_SLIDE:
		case SNAPPO_SWING:
			Put32(p + n, (DWORD)e.xSpeed);		n += 4;
			Put32(p + n, (DWORD)e.ySpeed);		n += 4;
			Put16(p + n, (WORD)e.direction);	n += 2;
			Put32(p + n, (DWORD)e.totalDist);	n += 4;
			Put32(p + n, (DWORD)e.tics);		n += 4;
			Put32(p + n, (DWORD)e.waitTics);	n += 4;
			p[n++] = e.close;
			break;
		default:
			break;
		}
	}
	return n;
}

// False on a body MoverPolyBodyBytes refuses or that disagrees with
// `bodyBytes`, fields a kind does not carry reading as 0.
inline bool ReadMoverPoly(const BYTE* p, const SnapFrame& f,
	unsigned int bodyBytes, SnapMoverPoly& s)
{
	if (MoverPolyBodyBytes(f, p, bodyBytes) != bodyBytes)
		return false;
	unsigned int n = 0;
	s.x		= ReadFixedIn(p + n, f, 0);	n += FixedBytes(f, 0);
	s.y		= ReadFixedIn(p + n, f, 1);	n += FixedBytes(f, 1);
	s.angle	= (angle_t)Read32(p + n);	n += 4;
	s.count	= p[n++];
	for (unsigned int i = 0; i < s.count; ++i)
	{
		SnapPolyEntry & e = s.e[i];
		e = SnapPolyEntry();
		e.kind	= p[n++];
		e.gen	= p[n++];
		e.speed	= (int)Read32(p + n);	n += 4;
		e.dist	= (int)Read32(p + n);	n += 4;
		switch (e.kind & SNAPPO_TYPEMASK)
		{
		case SNAPPO_MOVE:
			e.angle		= (SWORD)Read16(p + n);	n += 2;
			e.xSpeed	= ReadFixed32(p + n);	n += 4;
			e.ySpeed	= ReadFixed32(p + n);	n += 4;
			break;
		case SNAPPO_MOVETO:
			e.xSpeed	= ReadFixed32(p + n);	n += 4;
			e.ySpeed	= ReadFixed32(p + n);	n += 4;
			e.xTarget	= ReadFixedIn(p + n, f, 0);	n += FixedBytes(f, 0);
			e.yTarget	= ReadFixedIn(p + n, f, 1);	n += FixedBytes(f, 1);
			break;
		case SNAPPO_SLIDE:
		case SNAPPO_SWING:
			e.xSpeed	= ReadFixed32(p + n);	n += 4;
			e.ySpeed	= ReadFixed32(p + n);	n += 4;
			e.direction	= (SWORD)Read16(p + n);	n += 2;
			e.totalDist	= (int)Read32(p + n);		n += 4;
			e.tics		= (int)Read32(p + n);		n += 4;
			e.waitTics	= (int)Read32(p + n);		n += 4;
			e.close		= p[n++];
			break;
		default:	// ROTATE carries nothing more; the walk refused the rest
			break;
		}
	}
	return true;
}

// Body bytes for a fixed-shape class and its flags (which pick a shape), or 0
// for a class this build does not know, which the reader refuses.
inline unsigned int MoverBodyBytes(BYTE cls, BYTE flags)
{
	switch (cls)
	{
	case SNAPMV_PLAT:	return SNAPMV_PLAT_BYTES;
	case SNAPMV_CEILING:	return SNAPMV_CEILING_BYTES;
	case SNAPMV_DOOR:	return SNAPMV_DOOR_BYTES;
	case SNAPMV_ELEVATOR:	return SNAPMV_ELEVATOR_BYTES;	// NOFLOOR/NOCEILING: no shape
	case SNAPMV_PILLAR:		return SNAPMV_PILLAR_BYTES;
	case SNAPMV_FLOOR:	return (flags & SNAPMVF_STAIR)
							? SNAPMV_FLOOR_STAIR_BYTES : SNAPMV_FLOOR_BYTES;
	default:			return 0;	// SNAPMV_POLY too: its length is in its body
	}
}

// The body length a reader uses: by class and flags, or walked for a
// polyobject, 0 refusing the record.
inline unsigned int MoverRecordBodyBytes(const SnapFrame& f, BYTE cls, BYTE flags,
	const BYTE* pBody, unsigned int avail)
{
	if (cls == SNAPMV_POLY)
		return MoverPolyBodyBytes(f, pBody, avail);
	return MoverBodyBytes(cls, flags);
}

}} // namespace zx::netsnapshot

// The sector planes a class moves, one list for both ends, tested by
// intersection (0 for a class without a line, which each end refuses).
#define SNAPMV_PLANE_FLOOR		0x01
#define SNAPMV_PLANE_CEILING	0x02
#define SNAPMV_PLANE_BOTH		( SNAPMV_PLANE_FLOOR | SNAPMV_PLANE_CEILING )

namespace zx { namespace netsnapshot {

inline BYTE MoverPlanes(BYTE cls)
{
	switch (cls)
	{
	case SNAPMV_PLAT:
	case SNAPMV_FLOOR:		return SNAPMV_PLANE_FLOOR;
	case SNAPMV_CEILING:
	case SNAPMV_DOOR:		return SNAPMV_PLANE_CEILING;
	case SNAPMV_ELEVATOR:
	case SNAPMV_PILLAR:		return SNAPMV_PLANE_BOTH;
	default:			return 0;
	}
}

// The planes one record speaks for: its class's, less any a server takeover
// narrowed off a GONE.
inline BYTE RecordPlanes(BYTE cls, BYTE flags)
{
	BYTE m = MoverPlanes(cls);
	if (flags & SNAPMVF_NOFLOOR)
		m &= (BYTE)~SNAPMV_PLANE_FLOOR;
	if (flags & SNAPMVF_NOCEILING)
		m &= (BYTE)~SNAPMV_PLANE_CEILING;
	return m;
}

}} // namespace zx::netsnapshot

typedef char snap_assert_mover_body[
	((SNAPMV_PLAT_BYTES <= SNAPMV_BODY_MAX)
	&& (SNAPMV_CEILING_BYTES <= SNAPMV_BODY_MAX)
	&& (SNAPMV_FLOOR_STAIR_BYTES <= SNAPMV_BODY_MAX)
	&& (SNAPMV_DOOR_BYTES <= SNAPMV_BODY_MAX)
	&& (SNAPMV_ELEVATOR_BYTES <= SNAPMV_BODY_MAX)
	&& (SNAPMV_PILLAR_BYTES <= SNAPMV_BODY_MAX)
	&& ((SNAPPO_HEADER_BYTES + 2 + SNAPPO_DOOR_BYTES) <= SNAPMV_BODY_MAX)
	&& (SNAPPO_MAXENTRIES <= 255)
	&& ((SNAPMV_HEADER_BYTES + SNAPMV_BODY_MAX) <= SNAP_EVLEN_MAX)) ? 1 : -1];

namespace zx { namespace netsnapshot {

// Writes a mover event's body (mask, length, envelope, body; the caller writes
// the id), taking `bodyBytes` so a body under the wrong class mismatches.
inline unsigned int WriteMoverBody(BYTE* p, BYTE cls, BYTE flags,
	WORD index, BYTE gen, const BYTE* pBody, unsigned int bodyBytes)
{
	p[0] = (BYTE)(SNAPGRP_EVENT | SNAPEV_MOVER);
	unsigned int n = SNAP_EVENT_MASK_BYTES;
	p[n++] = (BYTE)(SNAPMV_HEADER_BYTES + bodyBytes);
	n += WriteMoverHeader(p + n, cls, flags, index, gen);
	for (unsigned int b = 0; b < bodyBytes; ++b)
		p[n + b] = pBody[b];
	return n + bodyBytes;
}

}} // namespace zx::netsnapshot

// The stream's version, raised for a change of meaning as well as of shape; the
// history is in snap_format.md.
#define ZSNAP_VERSION		34
// The oldest stream this reader decodes (v22 gave APPEAR two widths), raised
// whenever an older stream would be misread rather than skipped.
#define ZSNAP_VERSION_MIN	22
#define ZSNAP_FLAG_ZSTD		( 1 << 0 )
#define ZSNAP_HEADER_SIZE	16		// magic, version, ticrate, flags, reserved
#define ZSNAP_HEADER_FRAME	15		// from v33: three 32-bit origins, three widths
#define ZSNAP_RECHDR_SIZE	18

namespace zx { namespace netsnapshot {

// The header's size for a file of `version`, so every reader takes it from the
// version rather than assuming the current one.
inline unsigned int ZsnapHeaderBytes(unsigned int version)
{
	return ZSNAP_HEADER_SIZE + ((version >= 33) ? ZSNAP_HEADER_FRAME : 0);
}

}} // namespace zx::netsnapshot

// A ceiling on one record's uncompressed payload, so a hostile file cannot
// drive an absurd allocation.
#define ZSNAP_MAX_RAWSIZE	( 64 * 1024 * 1024 )

// SVC2_SNAPSOUND flags; the channel travels as its low byte, CHAN_LOOP being
// the only higher bit carried.
#define SNAP_SOUND_POINT	0x01	// x, y, z, then the NetID to attach to once known
#define SNAP_SOUND_LOOP		0x02	// CHAN_LOOP (never on a point, as SoundPoint)
#define SNAP_SOUND_VOLUME	0x04	// volume byte follows; else 127
#define SNAP_SOUND_ATTEN	0x08	// attenuation byte follows; else ATTN_INT_NORM

// SVC2_SNAPCOSMETIC: the map stamp, a count, then each event's flags byte and
// body (snap_format.md).
#define SNAP_COSE_KIND_MASK		0x03
#define SNAP_COSE_COPY			0		// generic; the copy gets NETFL_CLIENTSIDEONLY
#define SNAP_COSE_PLAIN			1		// generic; a plain NetID-0 copy
#define SNAP_COSE_BLOOD			2		// P_SpawnBlood
#define SNAP_COSE_SPLATTER		3		// P_BloodSplatter (P_BloodSplatter2 with SPLATTER2)
#define SNAP_COSE_AGE_SHIFT		2		// tics old within the pump, 0-7
#define SNAP_COSE_AGE_MASK		0x1C
#define SNAP_COSE_STATE			0x20	// generic: state WORD, ancestor depth << 12 | offset
#define SNAP_COSE_BLOODPLAIN	0x20	// blood kinds: a plain NetID-0 copy
#define SNAP_COSE_FINEVEL		0x40	// generic: velocity 3 SHORTs at 1/256; else 3 bytes at 1/4
#define SNAP_COSE_SPLATTER2		0x40	// splatter: P_BloodSplatter2
#define SNAP_COSE_EXTRAS		0x80	// generic: an extras mask byte and its fields

// The longest generic body in the widest frame, every field present (31
// narrow).
#define SNAP_COSE_GENERIC_MAX	( 31 + 3 * (SNAP_FRAME_WIDEST - SNAP_FRAME_NARROW) )
#define SNAP_COSX_TRANSLATION	0x01	// type BYTE, index WORD
#define SNAP_COSX_ALPHA			0x02	// BYTE, 255 = opaque
#define SNAP_COSX_SCALE			0x04	// x, y WORD at 1/256
#define SNAP_COSX_STYLE			0x08	// LONG, FRenderStyle
#define SNAP_COSX_ACTED			0x10	// no field: the state's action ran on the server, whose
										// children and sounds went separately (a released hold)

// A cosmetic event's body in engine units, quantized by the writers.
struct SnapCosGeneric
{
	WORD		classId;
	fixed_t		x, y, z;			// whole units on the wire
	angle_t		angle;				// its top byte
	fixed_t		vel[3];				// 1/256 unit under SNAP_COSE_FINEVEL, else 1/4
	int			stateCode;			// -1 none; else ancestor depth << 12 | offset
	int			extras;				// SNAP_COSX_ mask, ACTED included; 0 none
	BYTE		transType;
	WORD		transIndex;
	fixed_t		alpha;				// a byte, 255 opaque
	fixed_t		scaleX, scaleY;		// 1/256
	DWORD		style;
};
struct SnapCosBlood
{
	fixed_t		x, y, z;			// whole units on the wire
	angle_t		dir;				// SNAP_COSE_BLOOD only: its top byte
	int			damage;				// SNAP_COSE_BLOOD only: 0-255
	WORD		originator;
};

// The readers' stream, as BYTESTREAM_s reads: past `end` a read returns -1
// and still advances, so a truncated body is found after it is read (p > end).
struct SnapCosCursor
{
	const BYTE*	p;
	const BYTE*	end;
};

namespace zx { namespace netsnapshot {

inline int CosRead8(SnapCosCursor& c)
{
	const int v = (c.p + 1 <= c.end) ? c.p[0] : -1;
	c.p += 1;
	return v;
}
inline int CosRead16(SnapCosCursor& c)
{
	const int v = (c.p + 2 <= c.end) ? (short)(c.p[0] + (c.p[1] << 8)) : -1;
	c.p += 2;
	return v;
}
inline int CosRead32(SnapCosCursor& c)
{
	const int v = (c.p + 4 <= c.end) ? (int)((DWORD)c.p[0] | ((DWORD)c.p[1] << 8)
		| ((DWORD)c.p[2] << 16) | ((DWORD)c.p[3] << 24)) : -1;
	c.p += 4;
	return v;
}
// A position on axis k in whole units, relative to the frame's origin on the
// wire, -1 relative read past the end as CosRead16 reads.
inline int CosReadPos(SnapCosCursor& c, const SnapFrame& f, unsigned int k)
{
	const unsigned int w = PosBytes(f, k);
	const SDWORD rel = (c.p + w <= c.end) ? (SDWORD)ReadSigned(c.p, w) : -1;
	c.p += w;
	return PosAbs(f, k, rel);
}
// [Kaori-Chani] Clamps in 64-bit, so a 48.16 value meets the wire's range without truncating on
// the way in.
inline long long CosClamp(long long in, long long lo, long long hi)
{
	return (in <= lo) ? lo : (in >= hi) ? hi : in;
}

// Writes a generic event of kind SNAP_COSE_COPY or SNAP_COSE_PLAIN, flags byte
// first with no age (the pump adds it), in at most SNAP_COSE_GENERIC_MAX bytes;
// bBig and bSmall say why the velocity went fine, for the census.
inline unsigned int CosWriteGeneric(BYTE* pOut, const SnapFrame& f, BYTE kind,
	const SnapCosGeneric& g, bool& bBig, bool& bSmall)
{
	BYTE* p = pOut;
	BYTE* const pFlags = p++;
	BYTE flags = kind;
	Put16(p, g.classId);						p += 2;
	p += PutPosIn(p, f, 0, FloorPosIn(f, 0, g.x));
	p += PutPosIn(p, f, 1, FloorPosIn(f, 1, g.y));
	p += PutPosIn(p, f, 2, FloorPosIn(f, 2, g.z));
	*p++ = (BYTE)(g.angle >> 24);

	// 1/4 unit a tic fits +-32; slow smoke and anything faster take 1/256.
	bBig = false;
	bSmall = false;
	for (unsigned int k = 0; k < 3; ++k)
	{
		const fixed_t a = abs(g.vel[k]);
		if (a > 32 * FRACUNIT)
			bBig = true;
		else if ((a != 0) && (a < FRACUNIT))
			bSmall = true;
	}
	if (bBig || bSmall)
	{
		flags |= SNAP_COSE_FINEVEL;
		for (unsigned int k = 0; k < 3; ++k)
		{
			Put16(p, (WORD)CosClamp((long long)(g.vel[k] >> 8), -32768, 32767));
			p += 2;
		}
	}
	else
	{
		for (unsigned int k = 0; k < 3; ++k)
		{
			// [Kaori-Chani] Rounded from the floor and the fraction, as QuantPosIn is.
			const long long rv = (long long)g.vel[k];
			*p++ = (BYTE)(signed char)CosClamp((rv >> 14)
				+ (((rv & 0x3FFF) >= 0x2000) ? 1 : 0), -127, 127);
		}
	}

	if (g.stateCode >= 0)
	{
		flags |= SNAP_COSE_STATE;
		Put16(p, (WORD)g.stateCode);			p += 2;
	}

	if (g.extras != 0)
	{
		flags |= SNAP_COSE_EXTRAS;
		*p++ = (BYTE)g.extras;
		if (g.extras & SNAP_COSX_TRANSLATION)
		{
			*p++ = g.transType;
			Put16(p, g.transIndex);			p += 2;
		}
		if (g.extras & SNAP_COSX_ALPHA)
		{
			// Clamped first: past 128.0 the multiply would overflow.
			const long long alpha = CosClamp((long long)g.alpha, 0, (long long)FRACUNIT);
			*p++ = (BYTE)((alpha * 255 + (long long)FRACUNIT / 2) / (long long)FRACUNIT);
		}
		if (g.extras & SNAP_COSX_SCALE)
		{
			Put16(p, (WORD)CosClamp((long long)(g.scaleX >> 8), 0, 65535));	p += 2;
			Put16(p, (WORD)CosClamp((long long)(g.scaleY >> 8), 0, 65535));	p += 2;
		}
		if (g.extras & SNAP_COSX_STYLE)
		{
			Put32(p, g.style);					p += 4;
		}
	}
	*pFlags = flags;
	return (unsigned int)(p - pOut);
}

// [Kaori-Chani] A signed field scaled up by a 64-bit multiply, since a negative value shifted left
// is undefined and the 32-bit reinterpret would zero-extend it into 48.16.
inline fixed_t CosScale(int v, int shift)
{
	return (fixed_t)((long long)v * (1LL << shift));
}

// Reads the generic body after its flags byte, every extras field read when the
// mask was read past the end (-1), as the stream would.
inline void CosReadGeneric(SnapCosCursor& c, const SnapFrame& f, int flags,
	SnapCosGeneric& g)
{
	g.classId = (WORD)CosRead16(c);
	g.x = CosScale(CosReadPos(c, f, 0), FRACBITS);
	g.y = CosScale(CosReadPos(c, f, 1), FRACBITS);
	g.z = CosScale(CosReadPos(c, f, 2), FRACBITS);
	g.angle = (angle_t)CosRead8(c) << 24;
	for (unsigned int k = 0; k < 3; ++k)
		g.vel[k] = (flags & SNAP_COSE_FINEVEL) ? CosScale(CosRead16(c), 8)
			: CosScale((signed char)CosRead8(c), 14);
	g.stateCode = (flags & SNAP_COSE_STATE) ? (WORD)CosRead16(c) : -1;
	g.extras = 0;
	g.transType = 0;
	g.transIndex = 0;
	g.alpha = 0;
	g.scaleX = g.scaleY = 0;
	g.style = 0;
	if (flags & SNAP_COSE_EXTRAS)
	{
		g.extras = CosRead8(c);
		if (g.extras & SNAP_COSX_TRANSLATION)
		{
			g.transType = (BYTE)CosRead8(c);
			g.transIndex = (WORD)CosRead16(c);
		}
		if (g.extras & SNAP_COSX_ALPHA)
			g.alpha = (CosRead8(c) * FRACUNIT + 127) / 255;
		if (g.extras & SNAP_COSX_SCALE)
		{
			g.scaleX = (WORD)CosRead16(c) << 8;
			g.scaleY = (WORD)CosRead16(c) << 8;
		}
		if (g.extras & SNAP_COSX_STYLE)
			g.style = (DWORD)CosRead32(c);
	}
}

// Writes a blood or splatter event, its flags byte (the kind,
// SNAP_COSE_BLOODPLAIN, SNAP_COSE_SPLATTER2) first, with a direction and damage
// for SNAP_COSE_BLOOD only.
inline unsigned int CosWriteBlood(BYTE* pOut, const SnapFrame& f, BYTE flags,
	const SnapCosBlood& b)
{
	BYTE* p = pOut;
	*p++ = flags;
	p += PutPosIn(p, f, 0, FloorPosIn(f, 0, b.x));
	p += PutPosIn(p, f, 1, FloorPosIn(f, 1, b.y));
	p += PutPosIn(p, f, 2, FloorPosIn(f, 2, b.z));
	if ((flags & SNAP_COSE_KIND_MASK) == SNAP_COSE_BLOOD)
	{
		*p++ = (BYTE)(b.dir >> 24);
		*p++ = (BYTE)CosClamp(b.damage, 0, 255);
	}
	Put16(p, b.originator);					p += 2;
	return (unsigned int)(p - pOut);
}

inline void CosReadBlood(SnapCosCursor& c, const SnapFrame& f, int flags,
	SnapCosBlood& b)
{
	b.x = CosScale(CosReadPos(c, f, 0), FRACBITS);
	b.y = CosScale(CosReadPos(c, f, 1), FRACBITS);
	b.z = CosScale(CosReadPos(c, f, 2), FRACBITS);
	b.dir = 0;
	b.damage = 0;
	if ((flags & SNAP_COSE_KIND_MASK) == SNAP_COSE_BLOOD)
	{
		b.dir = (angle_t)CosRead8(c) << 24;
		b.damage = CosRead8(c);
	}
	b.originator = (WORD)CosRead16(c);
}

}} // namespace zx::netsnapshot

// Sound-table answer on CLC_SNAPSHOTACK: answered, match, and the stamp's low 6
// bits, harmless when repeated since the client sends 0 until it has the table.
#define SNAP_SOUNDIDS_ANSWERED	0x80
#define SNAP_SOUNDIDS_MATCH		0x40
#define SNAP_SOUNDIDS_STAMP		0x3F

#endif // ZX_NETSNAPSHOT_FORMAT_H
