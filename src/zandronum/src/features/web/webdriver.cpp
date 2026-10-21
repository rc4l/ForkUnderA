// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <emscripten.h>

#include "features/web/webdriver.h"
#include "features/mainloop/mainloop.h"
#include "features/frame-limit/framelimit.h"
#include "features/framephase/framephase.h"
#include "features/web/computation/webargs_compute.h"
#include "d_main.h"   // [rc4l] CRestartException
#include "doomerrors.h"
#include "m_argv.h"
#include "m_misc.h"   // [rc4l] M_SaveDefaults
#include "c_dispatch.h"   // [rc4l] AddCommandString
#include "features/wadreload/zx_wadreload.h"   // [rc4l] PendingReloadMap
#include <stdlib.h>
#include <string>

namespace zx { namespace web {

static void (*g_frame)() = nullptr;
static void (*g_terms)() = nullptr;

// [rc4l] An engine error that escaped a call from the browser shows on the page and leaves through exit, as main does on the desktop, so the engine's shutdown still runs.
static void FatalExit(CDoomError &error)
{
	Fatal(error.GetMessage());
	exit(-1);
}

static long long g_frameEndUs = 0;

static void Frame()
{
	// [rc4l] features/frame-limit: a browser offers a frame each refresh, and one not yet due under vid_maxfps or cl_capfps is skipped.
	if (!zx::framelimit::SteppedFrameDue()) return;
	// [rc4l] The wait for this animation frame is a browser's wait to present, counted in that phase as a desktop's vsync is.
	if (g_frameEndUs > 0) zx::framephase::AddPhase(zx::framephase::kPhasePresent, g_frameEndUs, zx::framephase::NowUs());
	try
	{
		g_frame();
	}
	catch (CRestartException &)
	{
		// [rc4l] A restart is a page reload, which starts the engine again on the arguments it held, a wad_reload's new WAD set and map among them.
		emscripten_cancel_main_loop();
		std::vector<std::string> held;
		for (int i = 1; i < Args->NumArgs(); i++) held.push_back(Args->GetArg(i));
		const std::string json = ComputeArgsJson(ComputeRestartArgs(held, zx::wadreload::PendingReloadMap()));
		EM_ASM({ if (Module.fuaRestart) Module.fuaRestart(UTF8ToString($0)); else location.reload(); }, json.c_str());
	}
	catch (CDoomError &error)
	{
		FatalExit(error);
	}
	g_frameEndUs = zx::framephase::NowUs();
}

// [rc4l] The browser calls Frame once an animation frame, which is also when Diligent presents.
static void Drive(void (*frame)())
{
	g_frame = frame;
	emscripten_set_main_loop(Frame, 0, 0);
}

void InstallDriver(void (*terms)())
{
	g_terms = terms;
	zx::mainloop::SetDriver(Drive);
}

void AppendPageArgs(DArgs *args)
{
	// [rc4l] A page a restart reloaded passes the arguments the engine held, which already carry the page's own.
	if (EM_ASM_INT({ return Module.fuaRestartArgs ? 1 : 0; })) return;
	args->AppendArg("-offline");
	char *query = (char *)EM_ASM_PTR({ return stringToNewUTF8(location.search); });
	for (const std::string &arg : ComputeArgsFromQuery(query != nullptr ? query : ""))
		args->AppendArg(arg.c_str());
	free(query);
}

// [rc4l] A console command from the page (Module.fuaCommand), for its controls and for tests driving the engine from the browser.
extern "C" EMSCRIPTEN_KEEPALIVE void fua_command(const char *text)
{
	// [rc4l] AddCommandString edits the line as it splits it, so it gets a copy.
	std::string line = text != nullptr ? text : "";
	// [rc4l] Its errors are handled as the loop handles a console command's, since this runs between frames.
	try
	{
		AddCommandString(&line[0]);
	}
	catch (CRecoverableError &error)
	{
		D_RecoverFromError(error);
	}
	catch (CDoomError &error)
	{
		FatalExit(error);
	}
}

// [rc4l] The config written for the page's keeper as writeini writes it, but without the line writeini prints, which every hidden page repeated; 0 when it could not be.
extern "C" EMSCRIPTEN_KEEPALIVE int fua_write_config()
{
	return M_SaveDefaults(NULL) ? 1 : 0;
}

void Fatal(const char *message)
{
	emscripten_cancel_main_loop();
	EM_ASM({
		const text = UTF8ToString($0);
		if (Module.fuaFatal) Module.fuaFatal(text); else console.error(text);
	}, message != nullptr ? message : "");
}

void SetPageFullscreen(bool on)
{
	EM_ASM({
		if ($0 && !document.fullscreenElement) Module.canvas.requestFullscreen().catch(() => {});
		else if (!$0 && document.fullscreenElement) document.exitFullscreen().catch(() => {});
	}, on);
}

}} // namespace zx::web

extern "C" void __real_exit(int code);

// [rc4l] Every exit in the browser build lands here (linked with --wrap=exit), since a page runs no atexit handlers and keeps calling the frame: it ends the loop, runs the engine's shutdown (the config among it) and tells the page.
extern "C" void __wrap_exit(int code)
{
	static bool exiting = false;
	if (!exiting)
	{
		exiting = true;
		emscripten_cancel_main_loop();
		if (zx::web::g_terms != nullptr) zx::web::g_terms();
		EM_ASM({ if (Module.fuaExit) Module.fuaExit($0); }, code);
	}
	__real_exit(code);
}
