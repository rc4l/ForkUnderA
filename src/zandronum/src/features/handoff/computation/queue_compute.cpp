// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/handoff/computation/queue_compute.h"

#include <algorithm>
#include <mutex>

namespace zx {

namespace {
std::mutex &ListMutex()
{
	static std::mutex m;
	return m;
}
std::vector<QueueBase *> &List()
{
	static std::vector<QueueBase *> queues;
	return queues;
}
}

QueueBase::QueueBase(const char *name) : m_name(name)
{
	std::lock_guard<std::mutex> hold(ListMutex());
	List().push_back(this);
}

QueueBase::~QueueBase()
{
	std::lock_guard<std::mutex> hold(ListMutex());
	std::vector<QueueBase *> &queues = List();
	queues.erase(std::remove(queues.begin(), queues.end(), this), queues.end());
}

void QueueBase::ForEach(void (*each)(const QueueBase &queue, void *user), void *user)
{
	std::lock_guard<std::mutex> hold(ListMutex());
	for (QueueBase *q : List()) each(*q, user);
}

} // namespace zx
