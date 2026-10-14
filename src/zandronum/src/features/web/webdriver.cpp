// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <emscripten.h>

#include "features/web/webdriver.h"
#include "features/mainloop/mainloop.h"
#include "features/web/computation/webargs_compute.h"
#include "d_main.h"   // [rc4l] CRestartException
#include "doomerrors.h"
#include "m_argv.h"
#include <stdlib.h>
#include <string>

namespace zx { namespace web {

static void (*g_frame)() = nullptr;

static void Frame()
{
	try
	{
		g_frame();
	}
	catch (CRestartException &)
	{
		// [rc4l] A restart re-initialises everything, which a page reload does from the start with the page's own arguments.
		emscripten_cancel_main_loop();
		EM_ASM({ if (Module.fuaRestart) Module.fuaRestart(); else location.reload(); });
	}
	catch (CDoomError &error)
	{
		Fatal(error.GetMessage());
	}
}

// [rc4l] The browser calls Frame once an animation frame, which is also when Diligent presents.
static void Drive(void (*frame)())
{
	g_frame = frame;
	emscripten_set_main_loop(Frame, 0, 0);
}

void InstallDriver()
{
	zx::mainloop::SetDriver(Drive);
}

void AppendQueryArgs(DArgs *args)
{
	char *query = (char *)EM_ASM_PTR({ return stringToNewUTF8(location.search); });
	for (const std::string &arg : ComputeArgsFromQuery(query != nullptr ? query : ""))
		args->AppendArg(arg.c_str());
	free(query);
}

void Fatal(const char *message)
{
	emscripten_cancel_main_loop();
	EM_ASM({
		const text = UTF8ToString($0);
		if (Module.fuaFatal) Module.fuaFatal(text); else console.error(text);
	}, message != nullptr ? message : "");
}

}} // namespace zx::web
