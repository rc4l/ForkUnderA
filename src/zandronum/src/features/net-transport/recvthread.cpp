// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "networkheaders.h"

#include "features/net-transport/recvthread.h"

#include "i_system.h"
#include "features/net-transport/computation/netconditions_compute.h"
#include "features/telemetry/telemetry.h"

#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace zx::nettransport;

// [rc4l] Datagrams queued at most, the newest dropped past it, which a main thread stalled for seconds could reach.
const size_t kQueueCapacity = 4096;
// [rc4l] How long the thread waits on the socket before it looks at whether it should stop.
const int kSelectMs = 50;

std::mutex g_mutex;
std::condition_variable g_arrived;
HeldPackets g_queue;
std::thread g_thread;
std::atomic<bool> g_stop(false);
std::atomic<bool> g_running(false);
std::atomic<unsigned> g_dropped(0), g_droppedReported(0);

void ReadLoop(SOCKET sock)
{
	std::vector<unsigned char> buffer(131072);
	while (!g_stop.load())
	{
		fd_set readable;
		FD_ZERO(&readable);
		FD_SET(sock, &readable);
		timeval wait = { 0, kSelectMs * 1000 };
		if (select(static_cast<int>(sock) + 1, &readable, NULL, NULL, &wait) <= 0)
			continue;
		for (;;)
		{
			sockaddr_storage from;
#ifdef _WIN32
			int fromLength = sizeof(from);
#else
			socklen_t fromLength = sizeof(from);
#endif
			const long got = recvfrom(sock, reinterpret_cast<char *>(buffer.data()), static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr *>(&from), &fromLength);
			// [rc4l] Would-block ends the burst; a reset from a client that left, and any other error, is the main thread's recvfrom's to have seen, so the thread just reads on.
			if (got <= 0) break;
			const unsigned arrival = I_MSTime();
			std::lock_guard<std::mutex> lock(g_mutex);
			if (!ComputeAdmitDatagram(g_queue, kQueueCapacity)) { g_dropped++; continue; }
			ComputeHoldPacket(g_queue, arrival, 0, buffer.data(), static_cast<int>(got), reinterpret_cast<const unsigned char *>(&from), static_cast<int>(fromLength));
			g_arrived.notify_one();
		}
	}
}

} // namespace

namespace zx { namespace nettransport {

void StartReceiveThread(uintptr_t socket)
{
	if (g_running.load() || static_cast<SOCKET>(socket) == INVALID_SOCKET) return;
	g_stop = false;
	g_running = true;
	g_thread = std::thread(ReadLoop, static_cast<SOCKET>(socket));
}

void StopReceiveThread()
{
	if (!g_running.load()) return;
	g_stop = true;
	g_arrived.notify_all();
	if (g_thread.joinable()) g_thread.join();
	g_running = false;
	std::lock_guard<std::mutex> lock(g_mutex);
	g_queue.queue.clear();
}

bool ReceiveThreadRunning() { return g_running.load(); }

long TakeDatagram(unsigned char *bytes, size_t capacity, void *from, int fromCapacity, int &fromLength, unsigned &arrivalMs)
{
	const unsigned dropped = g_dropped.load();
	if (dropped != g_droppedReported.load())
	{
		ZX_COUNT("net/receive_dropped", "packets", dropped - g_droppedReported.load());
		g_droppedReported = dropped;
	}
	HeldPacket held;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!ComputeTakeDuePacket(g_queue, LLONG_MAX, held)) return 0;
	}
	if (held.bytes.size() > capacity || held.addressLength > fromCapacity) return 0;
	memcpy(bytes, held.bytes.data(), held.bytes.size());
	memcpy(from, held.address.data(), held.address.size());
	fromLength = held.addressLength;
	arrivalMs = static_cast<unsigned>(held.releaseMs);
	return static_cast<long>(held.bytes.size());
}

void WaitForDatagram(int ms)
{
	if (!g_running.load())
	{
		I_Sleep(ms);
		return;
	}
	std::unique_lock<std::mutex> lock(g_mutex);
	g_arrived.wait_for(lock, std::chrono::milliseconds(ms), [] { return !g_queue.queue.empty() || g_stop.load(); });
}

}} // namespace zx::nettransport
