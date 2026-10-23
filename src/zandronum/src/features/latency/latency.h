// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Latency to the screen: latency/input_to_frame_ms from an input event to the end of the first frame after a tic carries it, and latency/packet_to_frame_ms from a server packet applied on a client to the end of the frame that draws it.

#ifndef ZX_LATENCY_H
#define ZX_LATENCY_H

namespace zx { namespace latency {

// [rc4l] An input event the game took (D_ProcessEvents).
void NoteInput();

// [rc4l] A tic's command built (G_BuildTiccmd), which carries every input taken before it.
void NoteTicBuilt();

// [rc4l] A server packet parsed on a client (CLIENT_ParsePacket).
void NotePacketApplied();

// [rc4l] A frame ended (the main loop's frame boundary), which shows what was armed before it.
void FrameShown();

}} // namespace zx::latency

#endif // ZX_LATENCY_H
