// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Putting a launcher reply back together when it arrived in pieces.
//
// A UDP datagram has a hard size ceiling and no way to grow one, so every query protocol that
// outgrows a packet ends up splitting replies and numbering the pieces. Valve's A2S queries do it
// with a split-packet header; Quake-derived engines do the same. Zandronum's server side already
// implements it -- see the segment loop in sv_serverregistry.cpp -- and until now nothing on our side
// asked for it, so every reply had to fit one datagram and the field set kept growing.
//
// THE WIRE FORMAT, as the server writes it. Twelve bytes, then payload:
//
//   long   SERVER_LAUNCHER_CHALLENGE_SEGMENTED   (5660032)
//   byte   segment index, 0-based
//   byte   segment count
//   short  offset of this piece within the whole reply
//   short  length of this piece
//   short  total size of the whole reply
//   ...    `length` bytes of payload
//
// Two details that bite if missed. The offsets and sizes are 16-bit, so a reply cannot exceed 64 KB
// however many segments it is cut into -- that is the real ceiling, not the segment count. And the
// server does NOT write the ordinary SERVER_LAUNCHER_CHALLENGE long into a segmented reply (see the
// `if ( !bSegmentedResponse )` guard), so the reassembled buffer begins directly at the flags long
// rather than at a challenge the caller might expect to skip.
//
// COVERAGE IS TRACKED PER BYTE, not per segment index, and that is deliberate. Counting arrivals
// would call a reply complete when a peer sent segment 0 twice and segment 1 never -- the count
// matches, the buffer has a hole, and the hole parses as zeros. Marking bytes means gaps, overlaps
// and duplicates are all the same question, answered exactly.
//
// Everything here treats the input as hostile, because it is: these are unauthenticated datagrams
// from whoever felt like sending one. A declared size is bounded before it is allocated, a declared
// length must actually be present in the datagram, and a region must fall inside the reply it claims
// to belong to.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_SEGMENTREASSEMBLY_COMPUTE_H
#define ZX_SEGMENTREASSEMBLY_COMPUTE_H

#include <cstddef>
#include <vector>

namespace zx
{

// Bytes of header after the challenge long: index, count, offset, length, total.
const size_t kSegmentHeaderBytes = 8;

// The largest reply that can be described at all: offset, length and total are 16-bit on the wire.
const int kMaxSegmentedReplyBytes = 65535;

// The largest number of pieces, since the count is a single byte.
const int kMaxSegmentCount = 255;

struct SegmentHeader
{
	int index;
	int count;
	int offset;
	int length;
	int totalSize;

	SegmentHeader() : index(0), count(0), offset(0), length(0), totalSize(0) {}
};

enum class SegmentRead
{
	Ok,
	TooShort,			// the datagram does not even contain a header
	Malformed,			// self-contradictory: index past count, region outside the reply, and so on
};

// Read the header from `data`, which must point at the byte AFTER the challenge long. Validates the
// header against itself and against `length`, the bytes actually present -- a declared 4 KB piece in
// a 200-byte datagram is malformed, not a short read to be patched up later.
SegmentRead ReadSegmentHeader(const unsigned char *data, size_t length, SegmentHeader &out);

// A reply being rebuilt. One of these per server being talked to.
struct SegmentAssembly
{
	std::vector<unsigned char> data;
	std::vector<unsigned char> covered;		// one flag per byte; see the note above on why per byte
	int count;
	int totalSize;
	int coveredBytes;
	bool active;

	SegmentAssembly() : count(0), totalSize(0), coveredBytes(0), active(false) {}
};

enum class SegmentAdd
{
	Started,			// first piece of a new reply
	Accepted,			// added, more still wanted
	Complete,			// added, and the reply is now whole
	Duplicate,			// every byte of it was already held; ignored rather than rewritten
	Rejected,			// the piece does not belong to any reply we could be assembling
};

// Take one piece. `payload` points at the bytes following the header, `payloadLength` at how many are
// actually present.
//
// A piece whose count or total size disagrees with the assembly in progress starts a NEW one rather
// than being refused: the ordinary cause is a second reply arriving while the first was incomplete,
// which is a legitimate thing for a server to do and would otherwise deadlock the assembly until it
// timed out. A duplicate piece is ignored rather than rewritten -- a second copy with different
// bytes is not a retransmission, and the first one is the one we already trusted.
SegmentAdd AddSegment(SegmentAssembly &assembly, const SegmentHeader &header,
	const unsigned char *payload, size_t payloadLength);

// Whether every byte has arrived.
bool AssemblyIsComplete(const SegmentAssembly &assembly);

// Forget everything, so the next piece starts a new reply.
void ResetAssembly(SegmentAssembly &assembly);

} // namespace zx

#endif // ZX_SEGMENTREASSEMBLY_COMPUTE_H
