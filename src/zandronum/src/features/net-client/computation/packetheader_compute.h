// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What a packet from the server is by its first byte: unreliable (parsed at once), reliable (sequenced and stored), or stray (neither, and so carrying no sequence to store it by).

#ifndef ZX_PACKETHEADER_COMPUTE_H
#define ZX_PACKETHEADER_COMPUTE_H

namespace zx { namespace netclient {

enum PacketHeaderKind { kPacketUnreliable, kPacketReliable, kPacketStray };

PacketHeaderKind ComputePacketHeaderKind(int firstByte, int svcHeader, int svcUnreliable);

// [rc4l] The ring slot after this one, so a ring of this many slots stores that many packets before it reuses one.
unsigned ComputeNextPacketSlot(unsigned slot, unsigned slots);

// [rc4l] Whether writing writeSize bytes at writeStart lands on any of a stored packet's slotSize bytes at slotStart.
bool ComputeOverwritesSlot(long slotStart, long slotSize, long writeStart, long writeSize);

}} // namespace zx::netclient

#endif // ZX_PACKETHEADER_COMPUTE_H
