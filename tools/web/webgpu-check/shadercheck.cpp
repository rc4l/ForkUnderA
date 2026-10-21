// [rc4l] Every dumped engine shader through Diligent's WebGPU path (glslang to SPIR-V, Tint to WGSL), with each failure and the vertex outputs a stage uses.
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "GLSLangUtils.hpp"
#include "WGSLUtils.hpp"
#include "DataBlob.h"
#include "RefCntAutoPtr.hpp"
#include "DebugOutput.h"

using namespace Diligent;

static std::string g_messages;

static void OnMessage(DEBUG_MESSAGE_SEVERITY sev, const Char *msg, const Char *, const Char *, int)
{
	if (sev == DEBUG_MESSAGE_SEVERITY_INFO) return;
	g_messages += msg ? msg : "";
	g_messages += "\n";
}

static int MaxLocation(const std::string &wgsl, const char *structName)
{
	const size_t at = wgsl.find(std::string("struct ") + structName + " {");
	if (at == std::string::npos) return -1;
	const size_t end = wgsl.find('}', at);
	const std::string body = wgsl.substr(at, end - at);
	std::regex re("@location\\((\\d+)\\)");
	int maxLoc = -1;
	for (std::sregex_iterator it(body.begin(), body.end(), re), e; it != e; ++it)
		maxLoc = std::max(maxLoc, std::stoi((*it)[1]));
	return maxLoc;
}

int main(int argc, char **argv)
{
	SetDebugMessageCallback(OnMessage);
	GLSLangUtils::InitializeGlslang();
	int failed = 0;
	for (int i = 1; i < argc; ++i)
	{
		const std::string path = argv[i];
		std::ifstream in(path, std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string src = ss.str();
		const bool pixel = path.size() > 5 && path.compare(path.size() - 5, 5, ".frag") == 0;
		g_messages.clear();
		RefCntAutoPtr<IDataBlob> output;
		GLSLangUtils::GLSLtoSPIRVAttribs a;
		a.ShaderType = pixel ? SHADER_TYPE_PIXEL : SHADER_TYPE_VERTEX;
		a.ShaderSource = src.c_str();
		a.SourceCodeLen = (int)src.size();
		a.SourceName = path.c_str();
		a.Version = GLSLangUtils::SpirvVersion::Vk100;
		a.AssignBindings = true;
		a.ppCompilerOutput = &output;
		std::vector<unsigned int> spirv = GLSLangUtils::GLSLtoSPIRV(a);
		if (spirv.empty())
		{
			++failed;
			printf("FAIL glsl  %s\n", path.c_str());
			if (output) printf("%.*s\n", (int)output->GetSize(), (const char *)output->GetConstDataPtr());
			printf("%s", g_messages.c_str());
			continue;
		}
		std::vector<uint32_t> words(spirv.begin(), spirv.end());
		const std::string wgsl = ConvertSPIRVtoWGSL(words);
		if (wgsl.empty())
		{
			++failed;
			printf("FAIL wgsl  %s\n%s", path.c_str(), g_messages.c_str());
			continue;
		}
		std::ofstream(path + ".wgsl", std::ios::binary) << wgsl;
		const int loc = pixel ? -1 : MaxLocation(wgsl, "main_out");
		printf("ok         %s%s\n", path.c_str(), loc >= 0 ? (" (vertex outputs up to location " + std::to_string(loc) + ")").c_str() : "");
	}
	GLSLangUtils::FinalizeGlslang();
	printf("%d of %d failed\n", failed, argc - 1);
	return failed != 0;
}
