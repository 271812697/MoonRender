#pragma once
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "Matc.h"

/** #include expansion, the build-time replacement for what the runtime loader used to do.
 *
 * A package is final GLSL, so the #include lines have to be resolved before the stages are
 * generated: `:Shaders/...` and the -I directories are the roots, an optional IncludeResolver
 * supplies text instead of disk (the editor has no Shaders folder), and each file is expanded
 * once per stage so the headers cannot declare the same block twice.
 */
namespace Moon::Matc
{
	/** One resolved #include. */
	struct ResolvedInclude
	{
		std::filesystem::path path;  // the file on disk
		std::string relative;        // how #line should name it
		std::string text;            // supplied by the resolver, when it handled the include
		bool hasText = false;
	};

	/** The headers every generated stage already has: a material that includes one of them
	 * would declare the same UBO or helper twice. */
	bool IsPreludeHeader(const std::string& p_relative);

	/** Where one #include points, the way the engine's path parser resolves it. */
	bool ResolveInclude(
		const std::string& p_target,
		const std::vector<std::string>& p_dirs,
		const IncludeResolver& p_resolver,
		ResolvedInclude& p_out);

	/** Expands the #include lines of one stage, keeping the #line anchors straight. */
	bool ExpandIncludes(
		const std::string& p_text,
		const std::vector<std::string>& p_dirs,
		const IncludeResolver& p_resolver,
		const std::string& p_origin,
		std::set<std::string>& p_visited,
		std::string& p_out,
		std::string& p_error);
}
