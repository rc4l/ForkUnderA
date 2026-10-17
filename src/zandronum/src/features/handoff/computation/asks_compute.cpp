// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/handoff/computation/asks_compute.h"

namespace zx { namespace handoff {

void Asks::Ask(Fn fn, void *arg)
{
	Pending p = { fn, arg, false, nullptr };
	std::unique_lock<std::mutex> hold(m_mutex);
	m_toAsker.wait(hold, [this] { return m_pending == nullptr; });
	m_pending = &p;
	m_toServer.notify_all();
	m_toAsker.wait(hold, [&p] { return p.done; });
	if (p.error) std::rethrow_exception(p.error);
}

bool Asks::Serve()
{
	std::unique_lock<std::mutex> hold(m_mutex);
	return RunTaken(hold);
}

void Asks::Wake()
{
	std::lock_guard<std::mutex> hold(m_mutex);
	m_toServer.notify_all();
}

uint64_t Asks::Served() const
{
	std::lock_guard<std::mutex> hold(m_mutex);
	return m_served;
}

bool Asks::RunTaken(std::unique_lock<std::mutex> &hold)
{
	Pending *p = m_pending;
	if (p == nullptr) return false;
	m_pending = nullptr;
	hold.unlock();
	try
	{
		p->fn(p->arg);
	}
	catch (...)
	{
		p->error = std::current_exception();
	}
	hold.lock();
	p->done = true;
	m_served++;
	m_toAsker.notify_all();
	return true;
}

}} // namespace zx::handoff
