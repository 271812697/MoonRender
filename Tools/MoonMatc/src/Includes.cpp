// #include expansion for the compiler: see Includes.h. Split out of Matc.cpp, where the whole
// pipeline used to live (docs/MaterialPipeline.md).

#include "Includes.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "FileUtils.h"
#include "TextUtils.h"

namespace Moon::Matc
{
	// ---------------------------------------------------------------------------------
	// #include
	//
	// The runtime used to expand these (ShaderLoader's AssembleSource, the engine's answer
	// to filament's Includes.cpp). Once a shader is a package there is no runtime pass left
	// to do it - the GLSL goes straight to the driver - so the expansion moves here, where
	// it also gets the chance to drop the two headers the prelude already carries.
	// ---------------------------------------------------------------------------------

	/** The headers every generated stage already has: a material that includes one of them
	 * would declare the same UBO or helper twice. */
	bool IsPreludeHeader(const std::string& p_relative)
	{
		return p_relative == "Common/Buffers/EngineUBO.glsl"
			|| p_relative == "Common/Utils.glsl";
	}

	/** Where one #include points, the way the engine's path parser resolves it: a leading
	 * ':' marks an engine path, and the directories handed in with -I are the shader roots
	 * the ":Shaders/" prefix stands for. A relative include is tried against them too, which
	 * is what a file inside the tree gets for free.
	 *
	 * p_resolver runs first: a release build packs the headers into the executable and has no
	 * directory to look in, while a development build simply leaves the resolver empty. */
	bool ResolveInclude(
		const std::string& p_target,
		const std::vector<std::string>& p_dirs,
		const IncludeResolver& p_resolver,
		ResolvedInclude& p_out)
	{
		if (p_resolver)
		{
			std::string text;
			std::string origin;
			if (p_resolver(p_target, text, origin))
			{
				p_out.text = std::move(text);
				p_out.relative = origin.empty() ? p_target : origin;
				p_out.hasText = true;
				return true;
			}
		}

		std::string target = p_target;
		if (!target.empty() && target.front() == ':')
		{
			target.erase(target.begin());
		}
		std::replace(target.begin(), target.end(), '\\', '/');

		std::string withoutRoot = target;
		constexpr std::string_view kRoot = "Shaders/";
		if (withoutRoot.starts_with(kRoot))
		{
			withoutRoot.erase(0, kRoot.size());
		}

		for (const std::string& dir : p_dirs)
		{
			for (const std::string& candidate : { withoutRoot, target })
			{
				const std::filesystem::path path = std::filesystem::path(dir) / candidate;
				std::error_code error;
				if (std::filesystem::is_regular_file(path, error))
				{
					p_out.path = path;
					p_out.relative = candidate;
					return true;
				}
			}
		}
		return false;
	}

	/** Expands the #include lines of one stage and keeps the diagnostic anchors straight: an
	 * included file opens with its own "#line 1", and the line after it goes back to the file
	 * (and the line) the #include was written on.
	 *
	 * Each file is expanded once per stage - the headers have no include guards, and PBR.glsl
	 * reaches Utils.glsl a second time - and the two the prelude already carries are dropped
	 * instead of duplicated. */
	bool ExpandIncludes(
		const std::string& p_text,
		const std::vector<std::string>& p_dirs,
		const IncludeResolver& p_resolver,
		const std::string& p_origin,
		std::set<std::string>& p_visited,
		std::string& p_out,
		std::string& p_error)
	{
		std::string currentFile = p_origin;
		uint32_t nextLine = 1;

		std::istringstream stream(p_text);
		std::string line;
		while (std::getline(stream, line))
		{
			const std::string trimmed = Trim(line);
			if (trimmed.starts_with("#line "))
			{
				// "#line N file": the line after it is line N of that file.
				p_out += line;
				p_out += '\n';
				// The number after "#line " is the line the one after it will be.
				size_t at = 6;
				while (at < trimmed.size() && trimmed[at] == ' ') { ++at; }
				uint32_t number = 0;
				bool hasDigits = false;
				while (at < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[at])))
				{
					number = number * 10 + static_cast<uint32_t>(trimmed[at] - '0');
					++at;
					hasDigits = true;
				}
				if (hasDigits)
				{
					nextLine = number;
				}
				const size_t first = trimmed.find('"');
				const size_t last = trimmed.rfind('"');
				if (first != std::string::npos && last != std::string::npos && last > first)
				{
					currentFile = trimmed.substr(first + 1, last - first - 1);
				}
				continue;
			}

			if (trimmed.starts_with("#include"))
			{
				const size_t first = trimmed.find('"');
				const size_t last = trimmed.rfind('"');
				if (first == std::string::npos || last == std::string::npos || last <= first)
				{
					p_error = "malformed #include in " + currentFile + " (line " + std::to_string(nextLine) + ")";
					return false;
				}

				const std::string target = trimmed.substr(first + 1, last - first - 1);
				ResolvedInclude resolved;
				if (!ResolveInclude(target, p_dirs, p_resolver, resolved))
				{
					p_error = "cannot resolve #include \"" + target + "\" (" + currentFile
						+ ", line " + std::to_string(nextLine) + ")";
					return false;
				}

				// Consumed either way: the include line itself occupies a line of currentFile.
				++nextLine;

				if (IsPreludeHeader(resolved.relative) || p_visited.contains(resolved.relative))
				{
					// Already in this stage, as part of the prelude or from an earlier
					// #include: dropping the line is what keeps the declarations single.
					continue;
				}
				p_visited.insert(resolved.relative);

				std::string content;
				if (resolved.hasText)
				{
					content = std::move(resolved.text);
				}
				else
				{
					bool readOk = false;
					content = ReadFile(resolved.path, readOk);
					if (!readOk)
					{
						p_error = "cannot read \"" + resolved.path.string() + "\"";
						return false;
					}
				}

				std::string expanded;
				if (!ExpandIncludes(content, p_dirs, p_resolver, resolved.relative, p_visited, expanded, p_error))
				{
					return false;
				}

				p_out += "#line 1 \"" + resolved.relative + "\"\n";
				p_out += expanded;
				if (!expanded.empty() && expanded.back() != '\n')
				{
					p_out += '\n';
				}
				p_out += "#line " + std::to_string(nextLine) + " \"" + currentFile + "\"\n";
				continue;
			}

			p_out += line;
			p_out += '\n';
			++nextLine;
		}
		return true;
	}
}
