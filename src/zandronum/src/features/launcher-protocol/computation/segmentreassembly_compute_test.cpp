// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/launcher-protocol/computation/segmentreassembly_compute.h"

using zx::AddSegment;
using zx::AssemblyIsComplete;
using zx::kSegmentHeaderBytes;
using zx::ReadSegmentHeader;
using zx::ResetAssembly;
using zx::SegmentAdd;
using zx::SegmentAssembly;
using zx::SegmentHeader;
using zx::SegmentRead;
using std::vector;

namespace
{

// One segment datagram body, exactly as sv_serverregistry.cpp writes it -- everything after the
// challenge long. Payload is `length` bytes of `fill`, so a rebuilt reply can be checked byte for
// byte against what each piece claimed to carry.
vector<unsigned char> MakeSegment(int index, int count, int offset, int length, int total,
	unsigned char fill, int payloadBytes = -1)
{
	vector<unsigned char> out;
	out.push_back(static_cast<unsigned char>(index));
	out.push_back(static_cast<unsigned char>(count));
	out.push_back(static_cast<unsigned char>(offset & 0xFF));
	out.push_back(static_cast<unsigned char>((offset >> 8) & 0xFF));
	out.push_back(static_cast<unsigned char>(length & 0xFF));
	out.push_back(static_cast<unsigned char>((length >> 8) & 0xFF));
	out.push_back(static_cast<unsigned char>(total & 0xFF));
	out.push_back(static_cast<unsigned char>((total >> 8) & 0xFF));

	const int actual = (payloadBytes < 0) ? length : payloadBytes;
	for (int i = 0; i < actual; ++i)
		out.push_back(fill);

	return out;
}

// Parse then add, the way the driver does, so the tests exercise the pair rather than each alone.
SegmentAdd Feed(SegmentAssembly &assembly, const vector<unsigned char> &datagram)
{
	SegmentHeader header;
	if (ReadSegmentHeader(&datagram[0], datagram.size(), header) != SegmentRead::Ok)
		return SegmentAdd::Rejected;

	return AddSegment(assembly, header, &datagram[0] + kSegmentHeaderBytes,
		datagram.size() - kSegmentHeaderBytes);
}

SegmentRead HeaderOf(const vector<unsigned char> &datagram, SegmentHeader &out)
{
	return ReadSegmentHeader(&datagram[0], datagram.size(), out);
}

} // namespace

// ---------------------------------------------------------------- the header

TEST(SegmentHeaderRead, ReadsAWellFormedHeader)
{
	SegmentHeader header;
	ASSERT_EQ(SegmentRead::Ok, HeaderOf(MakeSegment(1, 3, 500, 200, 1200, 0xAB), header));
	EXPECT_EQ(1, header.index);
	EXPECT_EQ(3, header.count);
	EXPECT_EQ(500, header.offset);
	EXPECT_EQ(200, header.length);
	EXPECT_EQ(1200, header.totalSize);
}

TEST(SegmentHeaderRead, ReadsSizesAboveThirtyTwoThousandUnsigned)
{
	// The engine's own ReadShort casts to a SIGNED short, so a 40000-byte reply would come back
	// negative. A size is not a signed quantity, and 64 KB is the format's actual ceiling.
	SegmentHeader header;
	ASSERT_EQ(SegmentRead::Ok, HeaderOf(MakeSegment(0, 2, 40000, 100, 60000, 0x01), header));
	EXPECT_EQ(40000, header.offset);
	EXPECT_EQ(60000, header.totalSize);
}

TEST(SegmentHeaderRead, RejectsADatagramTooShortForAHeader)
{
	vector<unsigned char> tiny(kSegmentHeaderBytes - 1, 0);
	SegmentHeader header;
	EXPECT_EQ(SegmentRead::TooShort, HeaderOf(tiny, header));
}

TEST(SegmentHeaderRead, RejectsHeadersThatContradictThemselves)
{
	SegmentHeader header;

	EXPECT_EQ(SegmentRead::Malformed, HeaderOf(MakeSegment(0, 0, 0, 10, 100, 1), header))
		<< "a reply in zero pieces";
	EXPECT_EQ(SegmentRead::Malformed, HeaderOf(MakeSegment(3, 3, 0, 10, 100, 1), header))
		<< "piece 3 of 3 -- indices are 0-based";
	EXPECT_EQ(SegmentRead::Malformed, HeaderOf(MakeSegment(0, 1, 0, 10, 0, 1), header))
		<< "a reply of no bytes";
	EXPECT_EQ(SegmentRead::Malformed, HeaderOf(MakeSegment(0, 1, 0, 0, 100, 1), header))
		<< "a piece carrying nothing";
	EXPECT_EQ(SegmentRead::Malformed, HeaderOf(MakeSegment(0, 1, 100, 10, 100, 1), header))
		<< "starting at the end";
	EXPECT_EQ(SegmentRead::Malformed, HeaderOf(MakeSegment(0, 1, 95, 10, 100, 1), header))
		<< "running past the end";
}

TEST(SegmentHeaderRead, RejectsAPieceLargerThanTheDatagramCarryingIt)
{
	// A declared 500-byte piece inside a 20-byte datagram is a lie, not a short read to patch up.
	SegmentHeader header;
	EXPECT_EQ(SegmentRead::Malformed,
		HeaderOf(MakeSegment(0, 1, 0, 500, 500, 0xEE, /*payloadBytes=*/12), header));
}

// ---------------------------------------------------------------- reassembly

TEST(SegmentAssembly, TwoPiecesInOrderRebuildTheReply)
{
	SegmentAssembly assembly;
	EXPECT_EQ(SegmentAdd::Started, Feed(assembly, MakeSegment(0, 2, 0, 4, 8, 0xAA)));
	EXPECT_FALSE(AssemblyIsComplete(assembly));
	EXPECT_EQ(SegmentAdd::Complete, Feed(assembly, MakeSegment(1, 2, 4, 4, 8, 0xBB)));

	ASSERT_TRUE(AssemblyIsComplete(assembly));
	ASSERT_EQ(8u, assembly.data.size());
	for (int i = 0; i < 4; ++i)
		EXPECT_EQ(0xAA, assembly.data[i]);
	for (int i = 4; i < 8; ++i)
		EXPECT_EQ(0xBB, assembly.data[i]);
}

TEST(SegmentAssembly, PiecesArrivingOutOfOrderRebuildTheReply)
{
	// UDP does not promise order, so this is the ordinary case rather than an exotic one.
	SegmentAssembly assembly;
	EXPECT_EQ(SegmentAdd::Started, Feed(assembly, MakeSegment(2, 3, 8, 4, 12, 0xCC)));
	EXPECT_EQ(SegmentAdd::Accepted, Feed(assembly, MakeSegment(0, 3, 0, 4, 12, 0xAA)));
	EXPECT_EQ(SegmentAdd::Complete, Feed(assembly, MakeSegment(1, 3, 4, 4, 12, 0xBB)));

	ASSERT_TRUE(AssemblyIsComplete(assembly));
	EXPECT_EQ(0xAA, assembly.data[0]);
	EXPECT_EQ(0xBB, assembly.data[5]);
	EXPECT_EQ(0xCC, assembly.data[9]);
}

TEST(SegmentAssembly, ASinglePieceReplyIsCompleteImmediately)
{
	SegmentAssembly assembly;
	EXPECT_EQ(SegmentAdd::Complete, Feed(assembly, MakeSegment(0, 1, 0, 5, 5, 0x7F)));
	EXPECT_TRUE(AssemblyIsComplete(assembly));
}

TEST(SegmentAssembly, ARepeatedPieceIsIgnored)
{
	SegmentAssembly assembly;
	ASSERT_EQ(SegmentAdd::Started, Feed(assembly, MakeSegment(0, 2, 0, 4, 8, 0xAA)));
	EXPECT_EQ(SegmentAdd::Duplicate, Feed(assembly, MakeSegment(0, 2, 0, 4, 8, 0xAA)));
	EXPECT_FALSE(AssemblyIsComplete(assembly));
}

TEST(SegmentAssembly, ARepeatedPieceDoesNotOverwriteWhatWeAlreadyHeld)
{
	// A second copy carrying different bytes is not a retransmission. The first is the one already
	// trusted, and letting a later packet rewrite it would make the reply whatever arrived last.
	SegmentAssembly assembly;
	ASSERT_EQ(SegmentAdd::Started, Feed(assembly, MakeSegment(0, 2, 0, 4, 8, 0xAA)));
	EXPECT_EQ(SegmentAdd::Duplicate, Feed(assembly, MakeSegment(0, 2, 0, 4, 8, 0x99)));

	for (int i = 0; i < 4; ++i)
		EXPECT_EQ(0xAA, assembly.data[i]) << "byte " << i << " was rewritten";
}

TEST(SegmentAssembly, CountingArrivalsWouldCallThisCompleteAndItIsNot)
{
	// The reason coverage is tracked per BYTE rather than per segment index. Two pieces arrive for a
	// two-piece reply, so an arrival counter says "done" -- but they are the same piece twice, half
	// the buffer was never written, and the hole would parse as zeros.
	SegmentAssembly assembly;
	ASSERT_EQ(SegmentAdd::Started, Feed(assembly, MakeSegment(0, 2, 0, 4, 8, 0xAA)));
	ASSERT_EQ(SegmentAdd::Duplicate, Feed(assembly, MakeSegment(0, 2, 0, 4, 8, 0xAA)));

	EXPECT_FALSE(AssemblyIsComplete(assembly)) << "two arrivals, one piece, half a reply";
	EXPECT_EQ(4, assembly.coveredBytes);
}

TEST(SegmentAssembly, OverlappingPiecesStillCompleteExactlyOnce)
{
	// Overlap is not something the server does, but nothing stops a peer sending it, and the count
	// must not run past the total or complete early.
	SegmentAssembly assembly;
	ASSERT_EQ(SegmentAdd::Started, Feed(assembly, MakeSegment(0, 2, 0, 6, 8, 0xAA)));
	EXPECT_EQ(SegmentAdd::Complete, Feed(assembly, MakeSegment(1, 2, 4, 4, 8, 0xBB)));

	EXPECT_EQ(8, assembly.coveredBytes);
	EXPECT_EQ(0xAA, assembly.data[5]) << "the overlapped byte keeps its first value";
	EXPECT_EQ(0xBB, assembly.data[6]);
}

TEST(SegmentAssembly, AReplyOfADifferentShapeStartsOver)
{
	// The ordinary cause is a second reply arriving while the first was incomplete, which a server
	// is entitled to do -- refusing it would wedge the assembly until something timed it out.
	SegmentAssembly assembly;
	ASSERT_EQ(SegmentAdd::Started, Feed(assembly, MakeSegment(0, 3, 0, 4, 12, 0xAA)));
	EXPECT_EQ(SegmentAdd::Started, Feed(assembly, MakeSegment(0, 2, 0, 4, 8, 0xBB)));

	EXPECT_EQ(8, assembly.totalSize);
	EXPECT_EQ(4, assembly.coveredBytes) << "the old reply's bytes must not be carried over";
	EXPECT_EQ(0xBB, assembly.data[0]);
}

TEST(SegmentAssembly, ResetForgetsEverything)
{
	SegmentAssembly assembly;
	ASSERT_EQ(SegmentAdd::Started, Feed(assembly, MakeSegment(0, 2, 0, 4, 8, 0xAA)));
	ResetAssembly(assembly);

	EXPECT_FALSE(AssemblyIsComplete(assembly));
	EXPECT_EQ(0, assembly.coveredBytes);
	EXPECT_EQ(0, assembly.totalSize);
	EXPECT_TRUE(assembly.data.empty());
}

TEST(SegmentAssembly, RefusesAHandBuiltHeaderThatReadSegmentHeaderWouldHaveCaught)
{
	// AddSegment is public, so it re-checks rather than trusting that a header came from the parser.
	// Sizing an allocation from an unvalidated number is how a datagram becomes a memory problem.
	SegmentAssembly assembly;
	const vector<unsigned char> payload(16, 0xAA);

	SegmentHeader huge;
	huge.index = 0; huge.count = 1; huge.offset = 0; huge.length = 4;
	huge.totalSize = zx::kMaxSegmentedReplyBytes + 1;
	EXPECT_EQ(SegmentAdd::Rejected, AddSegment(assembly, huge, &payload[0], payload.size()));

	SegmentHeader negative;
	negative.index = 0; negative.count = 1; negative.offset = -4; negative.length = 4;
	negative.totalSize = 8;
	EXPECT_EQ(SegmentAdd::Rejected, AddSegment(assembly, negative, &payload[0], payload.size()));

	SegmentHeader past;
	past.index = 0; past.count = 1; past.offset = 6; past.length = 4; past.totalSize = 8;
	EXPECT_EQ(SegmentAdd::Rejected, AddSegment(assembly, past, &payload[0], payload.size()));

	SegmentHeader empty;
	empty.index = 0; empty.count = 1; empty.offset = 0; empty.length = 0; empty.totalSize = 8;
	EXPECT_EQ(SegmentAdd::Rejected, AddSegment(assembly, empty, &payload[0], payload.size()));

	SegmentHeader noPieces;
	noPieces.index = 0; noPieces.count = 0; noPieces.offset = 0; noPieces.length = 4;
	noPieces.totalSize = 8;
	EXPECT_EQ(SegmentAdd::Rejected, AddSegment(assembly, noPieces, &payload[0], payload.size()));

	EXPECT_FALSE(assembly.active) << "nothing was allocated from any of them";
}

TEST(SegmentAssembly, RefusesAPayloadShorterThanTheHeaderPromised)
{
	SegmentAssembly assembly;
	const vector<unsigned char> shortPayload(2, 0xAA);

	SegmentHeader header;
	header.index = 0; header.count = 1; header.offset = 0; header.length = 4; header.totalSize = 4;
	EXPECT_EQ(SegmentAdd::Rejected, AddSegment(assembly, header, &shortPayload[0],
		shortPayload.size()));
}

TEST(SegmentAssembly, RefusesANullPayload)
{
	SegmentAssembly assembly;
	SegmentHeader header;
	header.index = 0; header.count = 1; header.offset = 0; header.length = 4; header.totalSize = 4;
	EXPECT_EQ(SegmentAdd::Rejected, AddSegment(assembly, header, NULL, 4));
}

TEST(SegmentAssembly, RebuildsALargeReplyFromManyPieces)
{
	// Closer to the real thing: 40 KB in 1 KB pieces, delivered back to front. Exercises the 16-bit
	// offsets above 32767, where a signed read would have gone negative.
	const int total = 40 * 1024;
	const int piece = 1024;
	const int count = total / piece;

	SegmentAssembly assembly;
	for (int i = count - 1; i >= 0; --i)
	{
		const SegmentAdd result = Feed(assembly,
			MakeSegment(i, count, i * piece, piece, total, static_cast<unsigned char>(i & 0xFF)));
		ASSERT_NE(SegmentAdd::Rejected, result) << "piece " << i;
	}

	ASSERT_TRUE(AssemblyIsComplete(assembly));
	ASSERT_EQ(static_cast<size_t>(total), assembly.data.size());
	for (int i = 0; i < count; ++i)
		EXPECT_EQ(static_cast<unsigned char>(i & 0xFF), assembly.data[i * piece]) << "piece " << i;
}
