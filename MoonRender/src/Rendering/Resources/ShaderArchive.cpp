#include <Rendering/Resources/ShaderArchive.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
	std::string ToLower(std::string p_text)
	{
		std::transform(p_text.begin(), p_text.end(), p_text.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return p_text;
	}

	/** The key the archive holds a source under: lower case, '/'-separated, and relative to
	 * the shader folder.
	 *
	 * That makes a resource key (":Shaders\\Common\\Utils.glsl"), a path under the engine
	 * assets ("D:/.../Engine/Shaders/Common/Utils.glsl") and a bare name ("utils.glsl")
	 * all resolve to the same entry, which is what the call sites need: they disagree on
	 * separators, on case, and on how much of the path they build. */
	std::string ArchiveKey(std::string p_name)
	{
		std::replace(p_name.begin(), p_name.end(), '\\', '/');
		p_name = ToLower(p_name);

		if (!p_name.empty() && p_name.front() == ':')
		{
			p_name.erase(0, 1);
		}

		// Drop everything up to and including the shader folder, wherever the path put it.
		const std::string folder = "shaders/";
		const size_t folderAt = p_name.rfind(folder);
		if (folderAt != std::string::npos)
		{
			p_name.erase(0, folderAt + folder.size());
		}

		while (!p_name.empty() && (p_name.front() == '/' || p_name.front() == '.'))
		{
			p_name.erase(0, 1);
		}
		return p_name;
	}

	/** Printed once, the first time a source comes out of the archive: it is the only signal
	 * that the shaders really are linked into the executable and the folder is not being
	 * used, and a silent fallback to the disk masks a mispacked archive completely. */
	bool g_reportedArchiveUse = false;
}

namespace Rendering::Resources
{
	const ShaderFile* FindShaderFile(const std::string& p_name)
	{
		const std::string key = ArchiveKey(p_name);
		if (key.empty())
		{
			return nullptr;
		}

		const ShaderArchive& archive = GetShaderArchive();
		for (uint32_t i = 0; i < archive.count; ++i)
		{
			if (ArchiveKey(archive.files[i].name) == key)
			{
				return &archive.files[i];
			}
		}
		return nullptr;
	}

	bool ReadShaderSource(const std::string& p_path, std::string& p_text, std::string* p_origin)
	{
		// Builds that pack compiled packages under the shader name also pack the source text
		// under Source/<name>: prefer that here, this function is the "give me the source"
		// entry point (the loader asks FindShaderFile for the package itself).
		const std::string relative = ArchiveKey(p_path);
		if (!relative.empty())
		{
			if (const ShaderFile* source = FindShaderFile("Source/" + relative))
			{
				p_text.assign(reinterpret_cast<const char*>(source->data), source->size);
				if (p_origin != nullptr)
				{
					*p_origin = source->name;
				}
				if (!g_reportedArchiveUse)
				{
					g_reportedArchiveUse = true;
					std::cout << "[Shader] sources are read from the embedded archive ("
						<< GetShaderArchive().count << " files packed); "
						<< "the Shaders folder is not needed at run time." << std::endl;
				}
				return true;
			}
		}

		if (const ShaderFile* file = FindShaderFile(p_path))
		{
			p_text.assign(reinterpret_cast<const char*>(file->data), file->size);
			if (p_origin != nullptr)
			{
				*p_origin = file->name;
			}
			if (!g_reportedArchiveUse)
			{
				g_reportedArchiveUse = true;
				std::cout << "[Shader] sources are read from the embedded archive ("
					<< GetShaderArchive().count << " files packed); "
					<< "the Shaders folder is not needed at run time." << std::endl;
			}
			return true;
		}

		std::ifstream stream(p_path, std::ios::binary);
		if (!stream)
		{
			return false;
		}

		std::ostringstream buffer;
		buffer << stream.rdbuf();
		p_text = buffer.str();
		if (p_origin != nullptr)
		{
			*p_origin = p_path;
		}
		return true;
	}
}
