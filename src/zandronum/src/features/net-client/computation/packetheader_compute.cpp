// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-client/computation/packetheader_compute.h"

namespace zx { namespace netclient {

PacketHeaderKind ComputePacketHeaderKind(int firstByte, int svcHeader, int svcUnreliable)
{
	if (firstByte == svcUnreliable) return kPacketUnreliable;
	if (firstByte == svcHeader) return kPacketReliable;
	return kPacketStray;
}

}} // namespace zx::netclient
