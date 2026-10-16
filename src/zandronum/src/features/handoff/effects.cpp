// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/handoff/effects.h"
#include "features/handoff/seam.h"
#include "features/handoff/computation/queue_compute.h"

#include "c_console.h"

#include <string>

int PrintString (int printlevel, const char *outline);

namespace zx { namespace handoff {

namespace {

// [rc4l] One effect in the order the tic made it, a print or a UI effect.
struct Effect
{
	bool print;
	int printlevel;
	std::string text;
	UIEffect ui;
};

zx::Queue<Effect> &Effects()
{
	static zx::Queue<Effect> queue("handoff/effects");
	return queue;
}

} // namespace

bool Deferring()
{
	return OnGameThread() && !OnMainThread();
}

bool BesideTic()
{
	return ComputeBesideTic(OnMainThread(), OnGameThread(), CurrentPhase());
}

void QueuePrint(int printlevel, const char *text)
{
	Effect e;
	e.print = true;
	e.printlevel = printlevel;
	e.text = text;
	e.ui = UIEffect();
	Effects().Push(std::move(e));
}

void QueueUI(const UIEffect &effect)
{
	Effect e;
	e.print = false;
	e.printlevel = 0;
	e.ui = effect;
	Effects().Push(std::move(e));
}

void DrainEffects()
{
	ZX_SEAM("handoff/effects", Handoff);
	Effects().Drain([](const Effect &e)
	{
		if (e.print) PrintString(e.printlevel, e.text.c_str());
		else e.ui.run(e.ui.a, e.ui.b);
	});
}

}} // namespace zx::handoff
