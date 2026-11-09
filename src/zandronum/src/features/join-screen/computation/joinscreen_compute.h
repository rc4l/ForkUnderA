// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] The loading screen shown while joining a server: which stage it is at, what it says, how full the bar is, and where things go.
#ifndef ZX_JOINSCREEN_COMPUTE_H
#define ZX_JOINSCREEN_COMPUTE_H

#include <string>

namespace zx
{

// [Doruk-Aksoy] The client's connection state (CTS_*), mirrored so this unit needs no engine header.
enum class ConnectionPhase
{
	Disconnected,
	Connecting,
	Authenticating,
	RequestingSnapshot,
	ReceivingSnapshot,
	Active,
};

enum class JoinStage
{
	None,
	LoadingFiles,
	Connecting,
	Authenticating,
	ReceivingGame,
};

struct JoinScreenInputs
{
	bool enabled;          // [Doruk-Aksoy] fua_joinscreen.
	bool clientMode;       // [Doruk-Aksoy] This process is a client of some server.
	ConnectionPhase phase;
	bool joinInFlight;     // [Doruk-Aksoy] The browser started a join that has not landed, which spans the WAD reload before the connect.

	JoinScreenInputs() : enabled(true), clientMode(false), phase(ConnectionPhase::Disconnected), joinInFlight(false) {}
};

// [Doruk-Aksoy] None outside a join; a refused or dropped connect is None so the console shows the reason.
JoinStage ComputeJoinStage(const JoinScreenInputs &in);

// [Doruk-Aksoy] The bar's target fill in [0, 1] for a stage.
float ComputeJoinStageProgress(JoinStage stage);

// [Doruk-Aksoy] The status line, naming the server when known, with its colour codes removed.
std::string ComputeJoinStatus(JoinStage stage, const std::string &serverName);

// [Doruk-Aksoy] The shown fill moving toward the target, so stage jumps glide and the bar never runs backwards.
float ComputeJoinProgressStep(float shown, float target, int elapsedMs);

// [Doruk-Aksoy] Screen rows and the bar's rectangle, in pixels, for a screen and a scaled text line height.
struct JoinScreenLayout
{
	int bandY, bandH;
	int titleY;
	int statusY;
	int barX, barY, barW, barH;
	int hintY;
};

JoinScreenLayout ComputeJoinScreenLayout(int screenW, int screenH, int lineH);

// [Doruk-Aksoy] Whether text on a band of this colour should be dark, since STARTUPCOLORS backgrounds run from black to near white.
bool ComputeJoinTextIsDark(int r, int g, int b);

} // namespace zx

#endif // ZX_JOINSCREEN_COMPUTE_H
