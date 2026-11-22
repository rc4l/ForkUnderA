#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kaori Ishida
"""snap_payload.py - the entity-record walk, shared by both offline readers.

.zsnap and .csnap differ only in framing: the payload inside them is the same
stream of entity records. csnap2.py and zsnap_info.py both call walk() here,
so the record layout has one Python copy, and `--verify` checks this file's
constants against format.h.

A record is:

    id | mask | [classId, when ENTER] | [target NetID, when the classId's top
    bit is set] | group fields per mask

where `id` is a varint gap from the previous record's id in the same payload,
and the first record of a payload carries a raw 16-bit id instead. A payload
is one datagram, so the base restarts at every chunk, which is what lets a
chunk be decoded without the chunk before it.
"""

DELTA, FRAME, RESET, MIRROR, MAP = 1, 2, 3, 4, 5

G_POS, G_ANG, G_POSZ, G_APPEAR, G_VELMISC, G_ENTER, G_EVENT, G_RENDER = (
    1, 2, 4, 8, 16, 32, 64, 128)
GROUPMASK = G_POS | G_POSZ | G_ANG | G_VELMISC | G_APPEAR | G_RENDER

# The fixed groups' widths; APPEAR is 2 bytes, or 4 with its first byte's top
# bit set.
GB = {G_POS: 4, G_POSZ: 2, G_ANG: 3, G_VELMISC: 9, G_RENDER: 4}
APPEAR_NARROW = 2
APPEAR_WIDE = 4
APPEAR_WIDE_FLAG = 0x80

EVBYTES_BRANCH = 11
EVBYTES_PSPRITE = 8
EVBYTES_LOCAL = 31
EVBYTES = {0: EVBYTES_BRANCH, 1: EVBYTES_PSPRITE, 2: EVBYTES_LOCAL}
# The positioned event types, which take a byte more an axis on a wide one.
EV_BRANCH, EV_LOCAL = 0, 2

# The map's position frame (format.h), whose wide axis adds a byte to each
# position on it; the tables above are the narrow frame's.
FRAME_NARROW = 2
FRAME_WIDE = 3


class Frame(object):
    __slots__ = ('origin', 'width')

    def __init__(self, origin=(0, 0, 0), width=(FRAME_NARROW,) * 3):
        self.origin = tuple(origin)
        self.width = tuple(width)

    def extra(self, k):
        """Bytes axis k adds to a field over its narrow form."""
        return self.width[k] - FRAME_NARROW

    def wide(self):
        return any(w != FRAME_NARROW for w in self.width)


NARROW = Frame()


def frame_width_known(width):
    return width in (FRAME_NARROW, FRAME_WIDE)


def read_signed(buf, off, n):
    """A little-endian signed value of n bytes, as ReadSigned."""
    return int.from_bytes(bytes(buf[off:off + n]), 'little', signed=True)


def event_bytes(et, frame=NARROW):
    """A fixed-length type's payload bytes in `frame`, or None if unknown."""
    if et not in EVBYTES:
        return None
    extra = sum(frame.extra(k) for k in range(3)) if et in (EV_BRANCH, EV_LOCAL) else 0
    return EVBYTES[et] + extra

# Event types 0-15 are fixed length; 16-31 carry a length byte, so a reader can
# skip one it does not know (docs/net-snapshot/tools.md).
EV_SELFDESC = 0x10

# Self-describing types this decoder can interpret beyond skipping them.
EV_MOVER = EV_SELFDESC | 0

# Mover envelope: class, flags, index and generation, then a body whose length
# comes from the class, never the length byte (docs/net-snapshot/tools.md).
MV_HEADER_BYTES = 5
MV_BODY_MAX = 128
MV_PLAT = 0
MV_PLAT_BYTES = 29
MV_FLOOR = 1
MV_FLOOR_BYTES = 19
MV_FLOOR_STAIR_BYTES = 43
MV_ELEVATOR = 2
MV_ELEVATOR_BYTES = 24
MV_PILLAR = 3
MV_PILLAR_BYTES = 31
MV_CEILING = 4
MV_CEILING_BYTES = 35
MV_DOOR = 5
MV_DOOR_BYTES = 38
# [Kaori-Chani] Declared by the header and never written: an animated door is replicated by its
# effects, so no body has a shape here (format.h's MoverClassTracked excludes it).
MV_ANIMDOOR = 6
MV_NOBODY = (MV_ANIMDOOR,)
# One record per polyobject, every action on it, walked by mover_poly_body_bytes.
MV_POLY = 7
PO_HEADER_BYTES = 13
PO_ENTRY_BYTES = 10
PO_MOVE_BYTES = 20
PO_MOVETO_BYTES = 26
PO_DOOR_BYTES = 33
PO_TYPEMASK = 0x07
PO_SPECIAL = 0x08
PO_NOINTERP = 0x40
PO_BLOCKED = 0x80
PO_TYPENAME = ('rotate', 'move', 'moveto', 'slide', 'swing')
PO_TYPEBYTES = (PO_ENTRY_BYTES, PO_MOVE_BYTES, PO_MOVETO_BYTES, PO_DOOR_BYTES,
                PO_DOOR_BYTES)
PO_MAXENTRIES = (MV_BODY_MAX - PO_HEADER_BYTES) // PO_ENTRY_BYTES
MVF_STAIR = 0x10
# GONE only, two-plane classes: the record no longer speaks for that plane.
MVF_NOFLOOR = 0x20
MVF_NOCEILING = 0x40
MV_CLSNAME = {MV_PLAT: 'plat', MV_FLOOR: 'floor', MV_ELEVATOR: 'elevator',
              MV_PILLAR: 'pillar', MV_CEILING: 'ceiling', MV_DOOR: 'door',
              MV_ANIMDOOR: 'animdoor', MV_POLY: 'poly'}


def mover_body_bytes(cls, flags):
    """Body width for a class and its flags, or None for one this decoder does
    not know."""
    if cls == MV_PLAT:
        return MV_PLAT_BYTES
    if cls == MV_CEILING:
        return MV_CEILING_BYTES
    if cls == MV_DOOR:
        return MV_DOOR_BYTES
    if cls == MV_ELEVATOR:
        return MV_ELEVATOR_BYTES   # NOFLOOR/NOCEILING change no shape
    if cls == MV_PILLAR:
        return MV_PILLAR_BYTES
    if cls == MV_FLOOR:
        return MV_FLOOR_STAIR_BYTES if (flags & MVF_STAIR) else MV_FLOOR_BYTES
    return None

MVF_BLOCKED = 0x01
MVF_GONE = 0x02
MVF_UPDATE = 0x04
# The old polyobject form's bit, which mover_header refuses.
MVF_POLY_RETIRED = 0x08

IDGAP_MAXBYTES = 3
REC_IDBYTES_RAW = 2

# The wire version these tables describe, which both readers require.
WIRE_VERSION = 34
# The versions whose payloads these tables read: v33 changed only the .zsnap
# header, so a v32 payload is v33's, and v34's is v33's in a narrow frame.
WIRE_READABLE = (32, 33, 34)

# An enter's classId bit for a 2-byte target NetID, masked off the class.
CLASS_TARGET = 0x8000
CLASS_MASK = 0x7FFF


def mover_header(body, frame=NARROW):
    """(cls, flags, index, gen, body_bytes) from a SNAPEV_MOVER payload past its
    length byte, as walk() hands it, in the map's `frame`; None for a class this
    decoder does not know or a length that does not match it."""
    hdr = MV_HEADER_BYTES
    if len(body) < hdr:
        return None
    cls = body[0]
    flags = body[1]
    if flags & MVF_POLY_RETIRED:
        return None
    index = body[2] | (body[3] << 8)
    gen = body[4]
    if cls == MV_POLY:
        want = mover_poly_body_bytes(body[hdr:], frame)
    else:
        want = mover_body_bytes(cls, flags)
    if want is None or (hdr + want) != len(body):
        return None
    return (cls, flags, index, gen, body[hdr:])


def mover_plat(body):
    """The plat body as a dict. `body` is what mover_header returned."""
    def u32(o):
        return body[o] | (body[o+1] << 8) | (body[o+2] << 16) | (body[o+3] << 24)
    def s32(o):
        v = u32(o)
        return v - (1 << 32) if v & 0x80000000 else v
    def s16(o):
        v = body[o] | (body[o+1] << 8)
        return v - (1 << 16) if v & 0x8000 else v
    return dict(speed=s32(0), low=s32(4), high=s32(8), planeD=s32(12),
                wait=s16(16), count=s16(18), tag=s16(20), platID=s16(22),
                crush=s16(24), status=body[26], oldStatus=body[27],
                type=body[28])


def mover_ceiling(body):
    """The ceiling body as a dict, planeD the ceiling plane. `body` is what
    mover_header returned."""
    def u32(o):
        return body[o] | (body[o+1] << 8) | (body[o+2] << 16) | (body[o+3] << 24)
    def s32(o):
        v = u32(o)
        return v - (1 << 32) if v & 0x80000000 else v
    def s16(o):
        v = body[o] | (body[o+1] << 8)
        return v - (1 << 16) if v & 0x8000 else v
    def s8(o):
        return body[o] - 256 if body[o] & 0x80 else body[o]
    return dict(speed=s32(0), speed1=s32(4), speed2=s32(8), bottom=s32(12),
                top=s32(16), planeD=s32(20), tag=s16(24), ceilingID=s16(26),
                crush=s16(28), type=body[30], silent=body[31],
                hexencrush=body[32], direction=s8(33), oldDirection=s8(34))


def mover_door(body):
    """The door body as a dict. planeD is the CEILING plane; botSpot is an end
    of one of the sector's lines, line slot * 2, plus 1 for v2, or -1 for
    none. topCountdown is 0 outside directions 0 and 2."""
    def u32(o):
        return body[o] | (body[o+1] << 8) | (body[o+2] << 16) | (body[o+3] << 24)
    def s32(o):
        v = u32(o)
        return v - (1 << 32) if v & 0x80000000 else v
    def s16(o):
        v = body[o] | (body[o+1] << 8)
        return v - (1 << 16) if v & 0x8000 else v
    def s8(o):
        return body[o] - 256 if body[o] & 0x80 else body[o]
    return dict(speed=s32(0), topDist=s32(4), botDist=s32(8),
                oldFloorDist=s32(12), planeD=s32(16), topWait=s32(20),
                topCountdown=s32(24), botSpot=s32(28), lightTag=s16(32),
                doorID=s16(34), type=body[36], direction=s8(37))


def mover_elevator(body):
    """The elevator body as a dict: both planes, since it drives both."""
    def u32(o):
        return body[o] | (body[o+1] << 8) | (body[o+2] << 16) | (body[o+3] << 24)
    def s32(o):
        v = u32(o)
        return v - (1 << 32) if v & 0x80000000 else v
    def s16(o):
        v = body[o] | (body[o+1] << 8)
        return v - (1 << 16) if v & 0x8000 else v
    def s8(o):
        return body[o] - 256 if body[o] & 0x80 else body[o]
    return dict(speed=s32(0), floorDestDist=s32(4), ceilingDestDist=s32(8),
                floorPlaneD=s32(12), ceilingPlaneD=s32(16), elevatorID=s16(20),
                type=body[22], direction=s8(23))


def mover_pillar(body):
    """The pillar body as a dict. `blocked` is a plane mask (1 floor, 2
    ceiling): its planes stall independently."""
    def u32(o):
        return body[o] | (body[o+1] << 8) | (body[o+2] << 16) | (body[o+3] << 24)
    def s32(o):
        v = u32(o)
        return v - (1 << 32) if v & 0x80000000 else v
    def s16(o):
        v = body[o] | (body[o+1] << 8)
        return v - (1 << 16) if v & 0x8000 else v
    return dict(floorSpeed=s32(0), ceilingSpeed=s32(4), floorTarget=s32(8),
                ceilingTarget=s32(12), floorPlaneD=s32(16), ceilingPlaneD=s32(20),
                pillarID=s16(24), crush=s16(26), type=body[28],
                hexencrush=body[29], blocked=body[30])


def mover_floor(body, flags):
    """The floor body as a dict. The six stair fields are present only under
    SNAPMVF_STAIR, and are reported as None otherwise rather than as zero - the
    wire did not carry them."""
    def u32(o):
        return body[o] | (body[o+1] << 8) | (body[o+2] << 16) | (body[o+3] << 24)
    def s32(o):
        v = u32(o)
        return v - (1 << 32) if v & 0x80000000 else v
    def s16(o):
        v = body[o] | (body[o+1] << 8)
        return v - (1 << 16) if v & 0x8000 else v
    def s8(o):
        return body[o] - 256 if body[o] & 0x80 else body[o]
    d = dict(speed=s32(0), destDist=s32(4), planeD=s32(8), floorID=s16(12),
             crush=s16(14), type=body[16], hexencrush=body[17],
             direction=s8(18))
    names = ('orgDist', 'resetCount', 'delay', 'pauseTime', 'stepTime',
             'perStepTime')
    for i, nm in enumerate(names):
        d[nm] = s32(19 + i * 4) if (flags & MVF_STAIR) else None
    return d


def po_header_bytes(frame=NARROW):
    """A polyobject body's header in `frame`: x, y, angle and the count."""
    return PO_HEADER_BYTES + frame.extra(0) + frame.extra(1)


def po_entry_bytes(t, frame=NARROW):
    """One entry's bytes in `frame` by its type, or None for one unknown."""
    if t >= len(PO_TYPEBYTES):
        return None
    return PO_TYPEBYTES[t] + ((frame.extra(0) + frame.extra(1)) if t == 2 else 0)


def mover_poly_body_bytes(body, frame=NARROW):
    """A polyobject body's walked length in `frame`, or None if it overruns,
    names an unknown kind or counts past PO_MAXENTRIES. As
    MoverPolyBodyBytes."""
    hdr = po_header_bytes(frame)
    if len(body) < hdr:
        return None
    count = body[hdr - 1]
    if count > PO_MAXENTRIES:
        return None
    n = hdr
    for _ in range(count):
        if n >= len(body):
            return None
        b = po_entry_bytes(body[n] & PO_TYPEMASK, frame)
        if b is None or n + b > len(body):
            return None
        n += b
    return n


def mover_poly(body, frame=NARROW):
    """The polyobject body as a dict: x, y, angle and a list of entries, each
    with its type name and only the fields its kind carries. Positions are
    absolute 16.16, the frame's origin added back."""
    def u32(o):
        return body[o] | (body[o+1] << 8) | (body[o+2] << 16) | (body[o+3] << 24)
    def s32(o):
        v = u32(o)
        return v - (1 << 32) if v & 0x80000000 else v
    def s16(o):
        v = body[o] | (body[o+1] << 8)
        return v - (1 << 16) if v & 0x8000 else v
    def fixed(o, k):
        return read_signed(body, o, frame.width[k] + 2) + (frame.origin[k] << 16)
    xb, yb = frame.width[0] + 2, frame.width[1] + 2
    d = dict(x=fixed(0, 0), y=fixed(xb, 1), angle=u32(xb + yb), entries=[])
    n = po_header_bytes(frame)
    for _ in range(body[n - 1]):
        k = body[n]
        t = k & PO_TYPEMASK
        e = dict(type=PO_TYPENAME[t], special=bool(k & PO_SPECIAL),
                 nointerp=bool(k & PO_NOINTERP), blocked=bool(k & PO_BLOCKED),
                 gen=body[n+1], speed=s32(n+2), dist=s32(n+6))
        o = n + PO_ENTRY_BYTES
        if t == 1:
            e.update(angle=s16(o), xSpeed=s32(o+2), ySpeed=s32(o+6))
        elif t == 2:
            e.update(xSpeed=s32(o), ySpeed=s32(o+4), xTarget=fixed(o+8, 0),
                     yTarget=fixed(o+8+xb, 1))
        elif t in (3, 4):
            e.update(xSpeed=s32(o), ySpeed=s32(o+4), direction=s16(o+8),
                     totalDist=s32(o+10), tics=s32(o+14), waitTics=s32(o+18),
                     close=body[o+22])
        d['entries'].append(e)
        n += po_entry_bytes(t, frame)
    return d


def group_bytes_before_appear(mask, frame=NARROW):
    """Bytes ahead of APPEAR, from the mask and the frame alone, as APPEAR
    follows POS, POSZ, ANG and VELMISC."""
    n = sum(b for g, b in GB.items()
            if (mask & g) and g in (G_POS, G_POSZ, G_ANG, G_VELMISC))
    if mask & G_POS:
        n += frame.extra(0) + frame.extra(1)
    if mask & G_POSZ:
        n += frame.extra(2)
    return n


def decode_pos(mask, body, frame=NARROW):
    """(x, y, z) in absolute whole units from a record's groups, each None
    where its group is absent. POSZ follows POS, or leads when POS is absent."""
    x = y = z = None
    o = 0
    if mask & G_POS:
        x = read_signed(body, o, frame.width[0]) + frame.origin[0]
        o += frame.width[0]
        y = read_signed(body, o, frame.width[1]) + frame.origin[1]
        o += frame.width[1]
    if mask & G_POSZ:
        z = read_signed(body, o, frame.width[2]) + frame.origin[2]
    return x, y, z


def group_bytes(mask, buf=None, off=0, end=0, frame=NARROW):
    """Bytes the groups occupy, or None if APPEAR's flag byte, which sets its
    size, is not in hand."""
    pre = group_bytes_before_appear(mask, frame)
    total = pre
    if mask & G_APPEAR:
        if buf is None:
            return None
        if off + pre + 1 > end:
            return None
        total += (APPEAR_WIDE if (buf[off + pre] & APPEAR_WIDE_FLAG)
                  else APPEAR_NARROW)
    if mask & G_RENDER:
        total += GB[G_RENDER]
    return total


def read_id(buf, off, end, prev, first):
    """(id, bytes consumed), or (None, 0) if truncated or longer than the three
    bytes a 16-bit gap can need."""
    if first:
        if end - off < REC_IDBYTES_RAW:
            return None, 0
        return buf[off] | (buf[off + 1] << 8), REC_IDBYTES_RAW
    v = 0
    shift = 0
    n = 0
    while (off + n < end) and (n < IDGAP_MAXBYTES):
        b = buf[off + n]
        n += 1
        v |= (b & 0x7F) << shift
        if (b & 0x80) == 0:
            return (prev + (v & 0xFFFF)) & 0xFFFF, n
        shift += 7
    return None, 0


class Record(object):
    __slots__ = ('id', 'mask', 'kind', 'offset', 'idbytes', 'classid',
                 'target', 'evtype', 'body')

    def __init__(self, **kw):
        for k in self.__slots__:
            setattr(self, k, kw.get(k))


def walk(buf, on_record=None, on_error=None, frame=NARROW):
    """Walk one payload in the map's `frame`, returning the number of errors.

    `on_record(rec)` is called per record with a Record whose `kind` is 'enter',
    'dirty', 'leave' or 'event' and whose `body` is the raw group or event
    bytes, undecoded. The id base restarts at every payload, so payloads are
    walked one at a time, never concatenated.
    """
    errors = 0
    off = 0
    n = len(buf)
    prev = 0
    first = True

    def err(msg):
        if on_error is not None:
            on_error(msg)

    while off < n:
        start = off
        eid, used = read_id(buf, off, n, prev, first)
        if used == 0:
            err("malformed entity id at offset %d" % off)
            return errors + 1
        off += used
        prev = eid
        first = False
        if off + 1 > n:
            err("truncated entity header at offset %d" % off)
            return errors + 1
        raw_mask = buf[off]
        off += 1

        # EVENT before groups, whose flags a high event type's bits overlap.
        if raw_mask & G_EVENT:
            et = raw_mask & 0x1F
            if et & EV_SELFDESC:
                # Self-describing: skip it whether or not this reader knows it.
                if off + 1 > n:
                    err("truncated event length at offset %d" % off)
                    return errors + 1
                ln = buf[off]
                off += 1
                if (ln == 0) or (off + ln > n):
                    err("bad self-describing event length %d at offset %d"
                        % (ln, off))
                    return errors + 1
                if on_record is not None:
                    on_record(Record(id=eid, mask=raw_mask, kind='event',
                                     offset=start, idbytes=used, evtype=et,
                                     body=buf[off:off + ln]))
                off += ln
                continue
            need = event_bytes(et, frame)
            if need is None:
                err("unknown event type %d at offset %d; rest of payload "
                    "unparseable" % (et, start))
                return errors + 1
            if off + need > n:
                err("truncated event payload for id %d" % eid)
                return errors + 1
            if on_record is not None:
                on_record(Record(id=eid, mask=raw_mask, kind='event',
                                 offset=start, idbytes=used, evtype=et,
                                 body=buf[off:off + need]))
            off += need
            continue

        mask = raw_mask & GROUPMASK
        if mask == 0:
            if on_record is not None:
                on_record(Record(id=eid, mask=0, kind='leave', offset=start,
                                 idbytes=used, body=b''))
            continue

        classid = None
        target = None
        if raw_mask & G_ENTER:
            if off + 2 > n:
                err("truncated classId for id %d" % eid)
                return errors + 1
            classid = buf[off] | (buf[off + 1] << 8)
            off += 2
            if classid & CLASS_TARGET:
                if off + 2 > n:
                    err("truncated target for id %d" % eid)
                    return errors + 1
                target = buf[off] | (buf[off + 1] << 8)
                off += 2
                classid &= CLASS_MASK

        need = group_bytes(mask, buf, off, n, frame)
        if (need is None) or (off + need > n):
            err("truncated groups for id %d at offset %d" % (eid, off))
            return errors + 1
        if on_record is not None:
            on_record(Record(id=eid, mask=mask, offset=start, idbytes=used,
                             kind='enter' if classid is not None else 'dirty',
                             classid=classid, target=target,
                             body=buf[off:off + need]))
        off += need

    return errors


def verify_header(path):
    """Check every constant above, the id encoding included, against
    format.h."""
    import re
    import sys
    try:
        raw = open(path, 'rb').read()
    except OSError as e:
        sys.exit('cannot read %s: %s' % (path, e))
    # Refused before any comparison, or a wrong file reads as a layout mismatch.
    if raw[:4] in (b'CSNP', b'ZSNP'):
        sys.exit('%s is a recording; --verify takes the path to format.h' % path)
    src = raw.decode('utf-8', errors='ignore')
    if 'ZX_NETSNAPSHOT_FORMAT_H' not in src:
        sys.exit('%s is not format.h (it has no ZX_NETSNAPSHOT_FORMAT_H guard);'
                 ' --verify takes the path to it' % path)
    bad = []

    # Widths from their #defines, which GroupBytes sums.
    def define(nm):
        m = re.search(r'#define ' + nm + r'\s+(\d+)', src)
        return int(m.group(1)) if m else None

    for nm, want in (('SNAP_GRPB_POS', GB[G_POS]), ('SNAP_GRPB_POSZ', GB[G_POSZ]),
                     ('SNAP_GRPB_ANG', GB[G_ANG]),
                     ('SNAP_GRPB_VELMISC', GB[G_VELMISC]),
                     ('SNAP_GRPB_RENDER', GB[G_RENDER]),
                     ('SNAP_GRPB_APPEAR_NARROW', APPEAR_NARROW),
                     ('SNAP_GRPB_APPEAR_WIDE', APPEAR_WIDE)):
        got = define(nm)
        if got != want:
            bad.append('  %s: header %s, decoder %d' % (nm, got, want))

    m = re.search(r'SNAPEV_SELFDESC = (0x[0-9A-Fa-f]+)', src)
    if m is None:
        bad.append('  SNAPEV_SELFDESC: not in header')
    elif int(m.group(1), 16) != EV_SELFDESC:
        bad.append('  SNAPEV_SELFDESC: header %s, decoder 0x%02X'
                   % (m.group(1), EV_SELFDESC))

    m = re.search(r'#define SNAP_CLASS_TARGET\s+(0x[0-9A-Fa-f]+)', src)
    if m is None:
        bad.append('  SNAP_CLASS_TARGET: not in header')
    elif int(m.group(1), 16) != CLASS_TARGET:
        bad.append('  SNAP_CLASS_TARGET: header %s, decoder 0x%04X'
                   % (m.group(1), CLASS_TARGET))

    m = re.search(r'#define SNAP_APPEAR_WIDE_FLAG\s+(0x[0-9A-Fa-f]+)', src)
    if m is None:
        bad.append('  SNAP_APPEAR_WIDE_FLAG: not in header')
    elif int(m.group(1), 16) != APPEAR_WIDE_FLAG:
        bad.append('  SNAP_APPEAR_WIDE_FLAG: header %s, decoder 0x%02X'
                   % (m.group(1), APPEAR_WIDE_FLAG))

    # The frame's widths, and what a wide axis adds to the positioned events:
    # the event tables are the narrow frame's.
    for nm, want in (('SNAP_FRAME_NARROW', FRAME_NARROW), ('SNAP_FRAME_WIDE', FRAME_WIDE),
                     ('SNAP_EVBYTES_WIDE_EXTRA', 3 * (FRAME_WIDE - FRAME_NARROW))):
        got = define(nm)
        if got != want:
            bad.append('  %s: header %s, decoder %d' % (nm, got, want))

    for nm, want in (('BRANCH', EVBYTES_BRANCH), ('PSPRITE', EVBYTES_PSPRITE),
                     ('LOCAL', EVBYTES_LOCAL)):
        m = re.search(r'#define SNAP_EVBYTES_' + nm + r'\s+(\d+)', src)
        if m is None:
            bad.append('  EVBYTES_%s: not in header' % nm)
        elif int(m.group(1)) != want:
            bad.append('  EVBYTES_%s: header %s, decoder %d' % (nm, m.group(1), want))

    # [Kaori-Chani] Every event type the header declares is one this walk can size: a
    # self-describing one carries its own length, a fixed-length one must be in EVBYTES.
    types = {}
    for m in re.finditer(r'\bSNAPEV_([A-Z]+)\s*=\s*(SNAPEV_SELFDESC \| \d+|0x[0-9A-Fa-f]+|\d+)',
                         src):
        nm, expr = m.group(1), m.group(2)
        if nm in ('TYPEMASK', 'SELFDESC', 'MAX'):
            continue
        if expr.startswith('SNAPEV_SELFDESC'):
            types[nm] = EV_SELFDESC | int(expr.split('|')[1])
        else:
            types[nm] = int(expr, 16) if expr.startswith('0x') else int(expr)
    if not types:
        bad.append('  SNAPEV_*: no event type read from the header at all')
    for nm, val in sorted(types.items()):
        if (val < EV_SELFDESC) and (val not in EVBYTES):
            bad.append('  SNAPEV_%s = %d: a fixed-length event this decoder cannot size'
                       % (nm, val))

    for nm, want in (('POS', G_POS), ('ANG', G_ANG), ('POSZ', G_POSZ),
                     ('APPEAR', G_APPEAR), ('VELMISC', G_VELMISC),
                     ('ENTER', G_ENTER), ('EVENT', G_EVENT), ('RENDER', G_RENDER)):
        m = re.search(r'SNAPGRP_' + nm + r'\s*=\s*1 << (\d+)', src)
        if m and (1 << int(m.group(1))) != want:
            bad.append('  bit SNAPGRP_%s: header %d, decoder %d'
                       % (nm, 1 << int(m.group(1)), want))

    # The id encoding, its widths and then the varint's shape.
    for nm, want in (('SNAP_IDGAP_MAXBYTES', IDGAP_MAXBYTES),
                     ('SNAP_REC_IDBYTES_RAW', REC_IDBYTES_RAW)):
        m = re.search(r'#define ' + nm + r'\s+(\d+)', src)
        if m is None:
            bad.append('  %s: not in header' % nm)
        elif int(m.group(1)) != want:
            bad.append('  %s: header %s, decoder %d' % (nm, m.group(1), want))

    thresholds = re.findall(r'if \(gap < (0x[0-9A-Fa-f]+)\)', src)
    if thresholds[:2] != ['0x80', '0x4000']:
        bad.append('  IdGapBytes thresholds: header %s, decoder '
                   "['0x80', '0x4000']" % thresholds[:2])

    # The mover envelope, as a body can be split differently at equal lengths.
    for nm, want in (('SNAPMV_HEADER_BYTES', MV_HEADER_BYTES),
                     ('SNAPMV_BODY_MAX', MV_BODY_MAX)):
        got = define(nm)
        if got is None:
            bad.append('  %s: not in header' % nm)
        elif got != want:
            bad.append('  %s: header %s, decoder %d' % (nm, got, want))

    # One per class, from the header's own sum expression: a body width that
    # exists in two places is maintained in one.
    for nm, want in (('SNAPMV_PLAT_BYTES', MV_PLAT_BYTES),
                     ('SNAPMV_CEILING_BYTES', MV_CEILING_BYTES),
                     ('SNAPMV_FLOOR_BYTES', MV_FLOOR_BYTES),
                     ('SNAPMV_DOOR_BYTES', MV_DOOR_BYTES),
                     ('SNAPMV_ELEVATOR_BYTES', MV_ELEVATOR_BYTES),
                     ('SNAPMV_PILLAR_BYTES', MV_PILLAR_BYTES)):
        m = re.search(r'#define ' + nm + r'\s+(\([^)]*\))', src)
        if m is None:
            bad.append('  %s: not in header' % nm)
            continue
        try:
            if eval(m.group(1)) != want:
                bad.append('  %s: header %s = %d, decoder %d'
                           % (nm, m.group(1), eval(m.group(1)), want))
        except Exception:
            bad.append('  %s: header %s not evaluable' % (nm, m.group(1)))

    # Polyobject entry widths, from the header's sums; the derived ones refer
    # to SNAPPO_ENTRY_BYTES, which is substituted.
    for nm, want in (('SNAPPO_HEADER_BYTES', PO_HEADER_BYTES),
                     ('SNAPPO_ENTRY_BYTES', PO_ENTRY_BYTES),
                     ('SNAPPO_MOVE_BYTES', PO_MOVE_BYTES),
                     ('SNAPPO_MOVETO_BYTES', PO_MOVETO_BYTES),
                     ('SNAPPO_DOOR_BYTES', PO_DOOR_BYTES)):
        m = re.search(r'#define ' + nm + r'\s+(\([^)]*\))', src)
        if m is None:
            bad.append('  %s: not in header' % nm)
            continue
        expr = m.group(1).replace('SNAPPO_ENTRY_BYTES', str(PO_ENTRY_BYTES))
        try:
            if eval(expr) != want:
                bad.append('  %s: header %s = %d, decoder %d'
                           % (nm, m.group(1), eval(expr), want))
        except Exception:
            bad.append('  %s: header %s not evaluable' % (nm, m.group(1)))
    for nm, want in (('SNAPPO_TYPEMASK', PO_TYPEMASK), ('SNAPPO_SPECIAL', PO_SPECIAL),
                     ('SNAPPO_NOINTERP', PO_NOINTERP), ('SNAPPO_BLOCKED', PO_BLOCKED)):
        m = re.search(r'#define ' + nm + r'\s+(0x[0-9A-Fa-f]+)', src)
        if m is None or int(m.group(1), 16) != want:
            bad.append('  %s: header %s, decoder 0x%02X'
                       % (nm, m.group(1) if m else 'missing', want))
    if re.search(r'#define SNAPMVF_POLY\b', src):
        bad.append('  SNAPMVF_POLY: in header; 0x08 must stay unused')

    # The stair shape is a sum of the base define, so check it after the base.
    m = re.search(r'#define SNAPMV_FLOOR_STAIR_BYTES\s+\(SNAPMV_FLOOR_BYTES([^)]*)\)', src)
    if m is None:
        bad.append('  SNAPMV_FLOOR_STAIR_BYTES: not in header')
    else:
        try:
            if MV_FLOOR_BYTES + eval('0' + m.group(1)) != MV_FLOOR_STAIR_BYTES:
                bad.append('  SNAPMV_FLOOR_STAIR_BYTES: header disagrees with decoder %d'
                           % MV_FLOOR_STAIR_BYTES)
        except Exception:
            bad.append('  SNAPMV_FLOOR_STAIR_BYTES: header not evaluable')

    m = re.search(r'#define SNAPMVF_STAIR\s+(0x[0-9A-Fa-f]+)', src)
    if m is not None and int(m.group(1), 16) != MVF_STAIR:
        bad.append('  SNAPMVF_STAIR: header %s, decoder 0x%02x' % (m.group(1), MVF_STAIR))

    # Class numbering: a decoder with the right widths against the wrong class
    # ids reads every body through the wrong table.
    for nm, want in (('SNAPMV_PLAT', MV_PLAT), ('SNAPMV_FLOOR', MV_FLOOR),
                     ('SNAPMV_ELEVATOR', MV_ELEVATOR), ('SNAPMV_PILLAR', MV_PILLAR),
                     ('SNAPMV_CEILING', MV_CEILING), ('SNAPMV_DOOR', MV_DOOR)):
        m = re.search(r'\b' + nm + r'\b\s*=\s*(\d+)', src)
        if m is not None and int(m.group(1)) != want:
            bad.append('  %s: header %s, decoder %d' % (nm, m.group(1), want))
    # The enum is implicit past SNAPMV_PLAT = 0, so the polyobject's value is
    # its position in it.
    m = re.search(r'enum\s*\{\s*SNAPMV_PLAT = 0,(.*?)SNAPMV_COUNT', src, re.S)
    names = re.findall(r'\b(SNAPMV_\w+)\s*,', m.group(1)) if m else []
    if 'SNAPMV_POLY' not in names or names.index('SNAPMV_POLY') + 1 != MV_POLY:
        bad.append('  SNAPMV_POLY: not at %d in the class enum' % MV_POLY)
    # [Kaori-Chani] And every class in it is one this decoder places: sized by mover_body_bytes,
    # or named in MV_NOBODY as one the header declares and no writer emits.
    for i, nm in enumerate(names):
        if nm == 'SNAPMV_COUNT':
            continue
        if (i + 1) not in MV_CLSNAME:
            bad.append('  %s = %d: a mover class this decoder does not know' % (nm, i + 1))

    m = re.search(r'SNAPEV_MOVER = SNAPEV_SELFDESC \| (\d+)', src)
    if m is None:
        bad.append('  SNAPEV_MOVER: not in header')
    elif (EV_SELFDESC | int(m.group(1))) != EV_MOVER:
        bad.append('  SNAPEV_MOVER: header 0x%02X, decoder 0x%02X'
                   % (EV_SELFDESC | int(m.group(1)), EV_MOVER))

    for nm, want in (('BLOCKED', MVF_BLOCKED), ('GONE', MVF_GONE),
                     ('UPDATE', MVF_UPDATE),
                     ('NOFLOOR', MVF_NOFLOOR), ('NOCEILING', MVF_NOCEILING)):
        m = re.search(r'#define SNAPMVF_' + nm + r'\s+(0x[0-9A-Fa-f]+)', src)
        if m is None:
            bad.append('  SNAPMVF_%s: not in header' % nm)
        elif int(m.group(1), 16) != want:
            bad.append('  SNAPMVF_%s: header %s, decoder 0x%02X'
                       % (nm, m.group(1), want))

    m = re.search(r'#define ZSNAP_VERSION\s+(\d+)', src)
    if m is None:
        bad.append('  ZSNAP_VERSION: not found')
    elif int(m.group(1)) != WIRE_VERSION:
        bad.append('  ZSNAP_VERSION: header %s, decoder %d'
                   % (m.group(1), WIRE_VERSION))

    if bad:
        sys.exit('LAYOUT MISMATCH against %s:\n%s' % (path, '\n'.join(bad)))
    print('layout matches %s: wire v%d, 5 fixed group widths + APPEAR %d/%d, '
          '8 mask bits, %d event payloads, %d mover classes, id varint %d/%d'
          % (path, WIRE_VERSION, APPEAR_NARROW, APPEAR_WIDE, len(EVBYTES),
             len(MV_CLSNAME), REC_IDBYTES_RAW, IDGAP_MAXBYTES))
