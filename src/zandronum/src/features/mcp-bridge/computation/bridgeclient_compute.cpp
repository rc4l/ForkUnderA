// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/mcp-bridge/computation/bridgeclient_compute.h"

namespace zx { namespace mcp {

ClientFeed ComputeClientOpen(const char *token)
{
	ClientFeed c;
	c.authed = token == nullptr;
	return c;
}

std::string ComputeClientHello(int pid, bool tokenRequired)
{
	return "{\"t\":\"hello\",\"engine\":\"forkundera\",\"bridge\":\"2.0.0\",\"pid\":" + std::to_string(pid) +
		",\"auth\":" + (tokenRequired ? "true" : "false") +
		",\"caps\":[\"rpc\",\"events\",\"determinism\",\"snapshot\",\"session\"]}";
}

FeedResult ComputeClientFeed(ClientFeed &client, const char *data, size_t n, const char *token)
{
	FeedResult out;
	client.rx.append(data, n);
	size_t nl;
	while ((nl = client.rx.find('\n')) != std::string::npos)
	{
		std::string line = client.rx.substr(0, nl);
		client.rx.erase(0, nl + 1);
		if (!client.authed)
		{
			// [rc4l] The first line carrying a token decides; anything else before it is dropped unanswered.
			std::string shown;
			if (!GetStr(line, "token", shown))
				continue;
			client.authed = token != nullptr && shown == token;
			out.replies.push_back(client.authed ? "{\"t\":\"authed\"}" : "{\"t\":\"error\",\"error\":\"bad token\"}");
			continue;
		}
		RpcRequest req = ParseRequest(line);
		if (req.valid)
			out.requests.push_back(req);
	}
	return out;
}

}} // namespace zx::mcp
