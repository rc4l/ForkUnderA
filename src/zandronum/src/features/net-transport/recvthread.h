// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A server's receive thread: it waits on the game socket, stamps each datagram's arrival and queues it, so the main thread parses without polling and ping counts from when a reply arrived rather than when the tick got to it.

#ifndef ZX_RECVTHREAD_H
#define ZX_RECVTHREAD_H

#include <cstddef>
#include <cstdint>

namespace zx { namespace nettransport {

// [rc4l] Starts reading socket (a SOCKET) on its own thread, once; a second start while one runs does nothing.
void StartReceiveThread(uintptr_t socket);

// [rc4l] Stops it and waits for it, leaving what it queued unread.
void StopReceiveThread();

bool ReceiveThreadRunning();

// [rc4l] The oldest queued datagram into bytes (up to capacity) and from (up to fromCapacity), with its length and arrival on I_MSTime's clock; 0 when none is queued.
long TakeDatagram(unsigned char *bytes, size_t capacity, void *from, int fromCapacity, int &fromLength, unsigned &arrivalMs);

// [rc4l] Waits up to ms for a datagram, at once if one is queued, or sleeps ms when no thread runs.
void WaitForDatagram(int ms);

}} // namespace zx::nettransport

#endif // ZX_RECVTHREAD_H
