// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/federated-server-registry/computation/lanbroadcast_compute.h"

namespace zx
{

void ComputeSubnetBroadcast(const unsigned char ip[4], unsigned char out[4])
{
	// How many leading octets are the network number.
	//   Class A: 1 - 127     -> A.255.255.255
	//   Class B: 128 - 191   -> A.B.255.255
	//   Class C: 192 - 223   -> A.B.C.255
	// 0.x, 224+ (multicast/reserved) have no classful directed broadcast; fall back to limited.
	int networkOctets = 0;
	const unsigned char first = ip[0];
	if ((first >= 1) && (first <= 127))
		networkOctets = 1;
	else if ((first >= 128) && (first <= 191))
		networkOctets = 2;
	else if ((first >= 192) && (first <= 223))
		networkOctets = 3;

	if (networkOctets == 0)
	{
		out[0] = out[1] = out[2] = out[3] = 255;
		return;
	}

	for (int i = 0; i < 4; ++i)
		out[i] = (i < networkOctets) ? ip[i] : 255;
}

} // namespace zx
