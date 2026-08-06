// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/launcher-protocol/computation/segmentreassembly_compute.h"

#include <cstring>

namespace zx
{

namespace
{

// Little-endian, matching BYTESTREAM_s::WriteShort. Read UNSIGNED: the engine's ReadShort casts to a
// signed short, which would turn any total size above 32767 negative -- and a reply is allowed to be
// up to 65535 bytes, so that is not a theoretical range.
int ReadShortLE(const unsigned char *data)
{
	return data[0] | (data[1] << 8);
}

} // namespace

SegmentRead ReadSegmentHeader(const unsigned char *data, size_t length, SegmentHeader &out)
{
	if (length < kSegmentHeaderBytes)
		return SegmentRead::TooShort;

	SegmentHeader header;
	header.index = data[0];
	header.count = data[1];
	header.offset = ReadShortLE(data + 2);
	header.length = ReadShortLE(data + 4);
	header.totalSize = ReadShortLE(data + 6);

	// Self-consistency first, before anything is sized from these numbers. Note the upper bounds need
	// no checking HERE: count comes from one byte so it cannot exceed kMaxSegmentCount, and the sizes
	// come from 16-bit reads so they cannot exceed kMaxSegmentedReplyBytes. AddSegment does check
	// them, because a caller can hand it a header it built itself.
	if (header.count < 1)
		return SegmentRead::Malformed;
	if (header.index >= header.count)
		return SegmentRead::Malformed;
	if (header.totalSize < 1)
		return SegmentRead::Malformed;
	if (header.length < 1)
		return SegmentRead::Malformed;

	// The piece has to fall inside the reply it says it belongs to. Written as a subtraction rather
	// than offset + length so it cannot overflow on the way to being checked.
	if (header.offset >= header.totalSize)
		return SegmentRead::Malformed;
	if (header.length > (header.totalSize - header.offset))
		return SegmentRead::Malformed;

	// And it has to actually be here. A declared 4 KB piece inside a 200-byte datagram is a lie, not
	// a short read to be tolerated.
	if ((length - kSegmentHeaderBytes) < static_cast<size_t>(header.length))
		return SegmentRead::Malformed;

	out = header;
	return SegmentRead::Ok;
}

void ResetAssembly(SegmentAssembly &assembly)
{
	assembly.data.clear();
	assembly.covered.clear();
	assembly.count = 0;
	assembly.totalSize = 0;
	assembly.coveredBytes = 0;
	assembly.active = false;
}

bool AssemblyIsComplete(const SegmentAssembly &assembly)
{
	return assembly.active && (assembly.coveredBytes == assembly.totalSize);
}

SegmentAdd AddSegment(SegmentAssembly &assembly, const SegmentHeader &header,
	const unsigned char *payload, size_t payloadLength)
{
	if ((payload == NULL) || (payloadLength < static_cast<size_t>(header.length)))
		return SegmentAdd::Rejected;

	// Re-check the header against itself here too. AddSegment is public, and a caller that built a
	// SegmentHeader by hand must not be able to size an allocation from nonsense.
	if ((header.count < 1) || (header.totalSize < 1) ||
		(header.totalSize > kMaxSegmentedReplyBytes) || (header.length < 1) ||
		(header.offset < 0) || (header.offset >= header.totalSize) ||
		(header.length > (header.totalSize - header.offset)))
	{
		return SegmentAdd::Rejected;
	}

	bool starting = false;

	// A piece describing a different reply starts a new one. The ordinary cause is a second reply
	// arriving while the first was incomplete, which a server is entitled to do.
	if (!assembly.active || (assembly.count != header.count) ||
		(assembly.totalSize != header.totalSize))
	{
		ResetAssembly(assembly);
		assembly.data.assign(static_cast<size_t>(header.totalSize), 0);
		assembly.covered.assign(static_cast<size_t>(header.totalSize), 0);
		assembly.count = header.count;
		assembly.totalSize = header.totalSize;
		assembly.active = true;
		starting = true;
	}

	int freshBytes = 0;
	for (int i = 0; i < header.length; ++i)
	{
		const size_t at = static_cast<size_t>(header.offset + i);
		if (assembly.covered[at])
			continue;						// already held; the first copy is the one we trust

		assembly.data[at] = payload[static_cast<size_t>(i)];
		assembly.covered[at] = 1;
		++freshBytes;
	}

	if (freshBytes == 0)
		return SegmentAdd::Duplicate;

	assembly.coveredBytes += freshBytes;

	if (assembly.coveredBytes == assembly.totalSize)
		return SegmentAdd::Complete;

	return starting ? SegmentAdd::Started : SegmentAdd::Accepted;
}

} // namespace zx
