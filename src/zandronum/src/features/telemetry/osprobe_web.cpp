// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The browser probe: the wasm memory's size, the heap's bytes in use, the page's own count of every WebGPU buffer and texture made and neither destroyed nor collected (web/gpu.js) in all and by tag, and the JavaScript heap where the browser says.

#include "features/telemetry/osprobe.h"
#include "features/telemetry/telemetry.h"

#include <emscripten.h>
#include <emscripten/heap.h>
#include <malloc.h>
#include <cstdlib>

EM_JS(double, fua_page_gpu_bytes, (), {
	return Module.fuaGpuLedger ? Module.fuaGpuLedger.heldBytes() : -1;
});

EM_JS(char *, fua_page_gpu_tags, (), {
	return stringToNewUTF8(Module.fuaGpuLedger ? Module.fuaGpuLedger.tagText() : '');
});

EM_JS(double, fua_page_js_heap_bytes, (), {
	return (typeof performance !== 'undefined' && performance.memory) ? performance.memory.usedJSHeapSize : -1;
});

namespace zx { namespace telemetry {

namespace {

// [rc4l] What the page holds on the GPU by tag, which counts what Diligent makes for itself beside the backend's own (gpu/mem/tag/).
void PageGpuTags(std::vector<Reading> &out)
{
	char *text = fua_page_gpu_tags();
	const std::vector<Reading> tags = ComputeTabbedReadings(text, "gpu/page/", "bytes");
	free(text);
	out.insert(out.end(), tags.begin(), tags.end());
}

const bool s_pageTags = (RegisterBreakdown("gpu/page/", PageGpuTags), true);

} // namespace

void ReadOs(OsReadings &out, bool walkHeaps)
{
	out.residentBytes = (double)emscripten_get_heap_size();
	if (walkHeaps)
	{
		const struct mallinfo mi = mallinfo();
		out.heapBytes = (double)mi.uordblks;
	}
	out.gpuProcessBytes = fua_page_gpu_bytes();
	out.jsHeapBytes = fua_page_js_heap_bytes();
}

const char *OsProbeName()
{
	return "web";
}

int CaptureStack(void **, int)
{
	return 0;
}

std::vector<std::string> SymbolNames(const std::vector<void *> &addresses)
{
	return std::vector<std::string>(addresses.size());
}

}} // namespace zx::telemetry
