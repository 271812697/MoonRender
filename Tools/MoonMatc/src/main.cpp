// MoonMatc - the command line half of the offline shader pipeline (the counterpart of
// Filament's matc). Everything it does lives in Matc.cpp, which the editor links and calls to
// hot reload a material; this file is the argument parser and the exit code.
//
// Usage:
//   MoonMatc -i <file.ovfx> -o <file.ovshader> [-I <include dir>]...
//            [--dump-glsl <dir>] [--dump-json <file>] [-E]
//
// A file with no material { } block is copied through untouched, so the shaders could be moved
// over one at a time.
//
// See docs/ShaderPipeline.md for the format and the design it copies.

#include <iostream>
#include <string>
#include <vector>

#include "Matc.h"

int main(int argc, char* argv[])
{
	Moon::Matc::CompileOptions options;
	std::string inputPath;

	for (int i = 1; i < argc; ++i)
	{
		const std::string argument = argv[i];
		const auto value = [&](std::string& p_target) {
			if (i + 1 < argc) { p_target = argv[++i]; }
		};
		if (argument == "-i") { value(inputPath); }
		else if (argument == "-o") { value(options.outputPath); }
		else if (argument == "-I") { std::string dir; value(dir); options.includeDirs.push_back(dir); }
		else if (argument == "--dump-glsl") { value(options.dumpGlslDir); }
		else if (argument == "--dump-json") { value(options.dumpJsonPath); }
		else if (argument == "-E") { options.preprocessOnly = true; }
		else if (!argument.empty() && argument[0] == '-')
		{
			std::cerr << "[MoonMatc] unknown option " << argument << std::endl;
			return 1;
		}
	}

	if (inputPath.empty() || (options.outputPath.empty() && !options.preprocessOnly))
	{
		std::cerr << "Usage: MoonMatc -i <file.ovfx> -o <file.ovshader> "
			<< "[-I dir]... [--dump-glsl dir] [--dump-json file] [-E]" << std::endl;
		return 1;
	}

	const Moon::Matc::CompileResult result = Moon::Matc::CompileFile(inputPath, options);
	if (!result.success)
	{
		std::cerr << "[MoonMatc] " << inputPath << ": " << result.error << std::endl;
		return 1;
	}

	if (options.preprocessOnly)
	{
		std::cout << result.preprocessed << std::endl;
		return 0;
	}

	if (result.passthrough)
	{
		std::cout << "[MoonMatc] " << inputPath
			<< ": passthrough (no material { } block)" << std::endl;
		return 0;
	}

	std::cout << "[MoonMatc] " << inputPath << " -> " << options.outputPath
		<< " (" << result.materialName << ", " << result.variantCount << " variant(s), "
		<< result.package.size() << " bytes)" << std::endl;
	return 0;
}