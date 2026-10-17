// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include <cstring>

#include "features/mcp-bridge/computation/bridgeclient_compute.h"

using namespace zx::mcp;

namespace
{
	FeedResult Feed(ClientFeed &c, const char *text, const char *token)
	{
		return ComputeClientFeed(c, text, strlen(text), token);
	}
}

TEST(BridgeClient, OpenIsAuthedOnlyWithoutAToken)
{
	EXPECT_TRUE(ComputeClientOpen(nullptr).authed);
	EXPECT_FALSE(ComputeClientOpen("t0k").authed);
	EXPECT_TRUE(ComputeClientOpen("t0k").rx.empty());
}

TEST(BridgeClient, HelloSaysWhetherTheTokenIsStillRequired)
{
	EXPECT_EQ(ComputeClientHello(42, true),
		"{\"t\":\"hello\",\"engine\":\"forkundera\",\"bridge\":\"2.0.0\",\"pid\":42,\"auth\":true,"
		"\"caps\":[\"rpc\",\"events\",\"determinism\",\"snapshot\",\"session\"]}");
	EXPECT_NE(ComputeClientHello(7, false).find("\"pid\":7,\"auth\":false,"), std::string::npos);
}

// [rc4l] A request split across reads is held until its newline, and two in one read both come out in order.
TEST(BridgeClient, LinesSplitAndJoinedAcrossReads)
{
	ClientFeed c = ComputeClientOpen(nullptr);
	FeedResult r = Feed(c, "{\"id\":1,\"cmd\":\"pi", nullptr);
	EXPECT_TRUE(r.requests.empty());
	EXPECT_EQ(c.rx, "{\"id\":1,\"cmd\":\"pi");
	r = Feed(c, "ng\"}\n{\"id\":2,\"cmd\":\"sim.tic\"}\nnot a request\n{\"id\":3", nullptr);
	ASSERT_EQ(r.requests.size(), 2u);
	EXPECT_EQ(r.requests[0].id, 1);
	EXPECT_EQ(r.requests[0].cmd, "ping");
	EXPECT_EQ(r.requests[1].cmd, "sim.tic");
	EXPECT_TRUE(r.replies.empty());
	EXPECT_EQ(c.rx, "{\"id\":3");
}

// [rc4l] Before the token nothing runs: lines without one vanish, a wrong one is told so, and the right one opens the gate for the lines after it.
TEST(BridgeClient, TokenGatesEveryRequest)
{
	ClientFeed c = ComputeClientOpen("t0k");
	FeedResult r = Feed(c, "{\"id\":1,\"cmd\":\"ping\"}\n{\"id\":0,\"cmd\":\"auth\",\"args\":{\"token\":\"nope\"}}\n", "t0k");
	EXPECT_TRUE(r.requests.empty());
	ASSERT_EQ(r.replies.size(), 1u);
	EXPECT_EQ(r.replies[0], "{\"t\":\"error\",\"error\":\"bad token\"}");
	EXPECT_FALSE(c.authed);
	r = Feed(c, "{\"id\":0,\"cmd\":\"auth\",\"args\":{\"token\":\"t0k\"}}\n{\"id\":2,\"cmd\":\"ping\"}\n", "t0k");
	ASSERT_EQ(r.replies.size(), 1u);
	EXPECT_EQ(r.replies[0], "{\"t\":\"authed\"}");
	EXPECT_TRUE(c.authed);
	ASSERT_EQ(r.requests.size(), 1u);
	EXPECT_EQ(r.requests[0].id, 2);
}

// [rc4l] A connection that was told no token is needed but shows one anyway is refused rather than trusted.
TEST(BridgeClient, ATokenShownWhereNoneIsSetIsRefused)
{
	ClientFeed c;
	FeedResult r = Feed(c, "{\"cmd\":\"auth\",\"args\":{\"token\":\"x\"}}\n", nullptr);
	ASSERT_EQ(r.replies.size(), 1u);
	EXPECT_EQ(r.replies[0], "{\"t\":\"error\",\"error\":\"bad token\"}");
	EXPECT_FALSE(c.authed);
}
