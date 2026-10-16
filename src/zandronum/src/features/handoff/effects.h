// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What a tic running on a thread of its own does to the main thread's state, queued and done in the handoff in the order the tic did it: its prints and the UI it asks for (menus, the automap, the console).

#ifndef ZX_HANDOFF_EFFECTS_H
#define ZX_HANDOFF_EFFECTS_H

#include <cstdint>
#include <vector>

namespace zx { namespace handoff {

// [rc4l] Whether the calling code is a tic on a thread of its own, whose effects on the main thread wait for the handoff; inline tics act at once.
bool Deferring();

// [rc4l] Whether the calling code is the main thread drawing beside a tic on the game thread, whose changes to game data wait for the handoff too.
bool BesideTic();

// [rc4l] A print of a tic on its own thread, put on the console in the handoff.
void QueuePrint(int printlevel, const char *text);

// [rc4l] A UI effect: run(a, b) in the handoff.
struct UIEffect
{
	void (*run)(intptr_t a, intptr_t b);
	intptr_t a, b;
};
void QueueUI(const UIEffect &effect);

// [rc4l] Every queued print and UI effect, in order; the handoff's, after each tic.
void DrainEffects();

// [rc4l] Values a tic makes for the main thread's state, applied at once inline and, from a tic on its own thread, in the handoff in the tic's order.
template <typename T>
class Deferred
{
public:
	explicit Deferred(void (*apply)(const T &)) : m_apply(apply) {}

	void Make(const T &value)
	{
		if (!Deferring()) { m_apply(value); return; }
		if (m_pending.empty()) QueueUI({ &Deferred::ApplyAll, (intptr_t)this, 0 });
		m_pending.push_back(value);
	}

	// [rc4l] Forgets what is waiting, as a level's end forgets what it made.
	void Clear() { m_pending.clear(); }

private:
	static void ApplyAll(intptr_t self, intptr_t)
	{
		Deferred *d = (Deferred *)self;
		for (size_t i = 0; i < d->m_pending.size(); i++) d->m_apply(d->m_pending[i]);
		d->m_pending.clear();
	}

	void (*m_apply)(const T &);
	std::vector<T> m_pending;   // [rc4l] the game thread's in a tic, the handoff's after it, never both at once
};

}} // namespace zx::handoff

#endif // ZX_HANDOFF_EFFECTS_H
