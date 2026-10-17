// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What one bridge connection has sent, read without sockets so the framing and the token gate are tested.

#ifndef ZX_BRIDGECLIENT_COMPUTE_H
#define ZX_BRIDGECLIENT_COMPUTE_H

#include <cstddef>
#include <string>
#include <vector>
#include "features/mcp-bridge/computation/mcprpc_compute.h"

namespace zx { namespace mcp {

// [rc4l] A connection's bytes not yet ended by a newline, and whether it has shown the token.
struct ClientFeed
{
	std::string rx;
	bool        authed = false;
};

// [rc4l] The requests a batch of bytes completed, and the lines that answer its sender at once.
struct FeedResult
{
	std::vector<RpcRequest>  requests;
	std::vector<std::string> replies;
};

// [rc4l] A new connection, already authed when token is null because none is required.
ClientFeed ComputeClientOpen(const char *token);

// [rc4l] The greeting a connection gets, whose "auth" says whether it still has to show the token.
std::string ComputeClientHello(int pid, bool tokenRequired);

// [rc4l] Appends bytes from one connection and returns what its complete lines ask, dropping every line before the right token.
FeedResult ComputeClientFeed(ClientFeed &client, const char *data, size_t n, const char *token);

}} // namespace zx::mcp

#endif
