#pragma once
#include <cstdint>
#include <string>

namespace Rendering::Resources
{
	/** One shader source the build packed into the executable. */
	struct ShaderFile
	{
		/** Path relative to the shader folder, as it is on disk ("Common/Utils.glsl"). */
		const char* name;
		/** The text itself, exactly as the file holds it, not null terminated. */
		const uint8_t* data;
		uint32_t size;
	};

	/** Everything the pack step embedded: every .shader and .glsl of the shader folder. */
	struct ShaderArchive
	{
		const ShaderFile* files = nullptr;
		uint32_t count = 0;
	};

	/** The archive above, implemented by the generated shader.cpp. */
	const ShaderArchive& GetShaderArchive();

	/** Finds a source by any of the names the loaders are handed - a resource key such as
	 * ":Shaders\\Standard.shader", a path under the engine assets, or a bare file name.
	 *
	 * The call sites are not consistent about separators, about the case of a file name or
	 * about how much of the path they carry, so all of that is normalized away here rather
	 * than at each of them.
	 * @return nullptr when the archive does not hold it. */
	const ShaderFile* FindShaderFile(const std::string& p_name);

	/** Reads one shader source: from the archive when this build embedded it, and from the
	 * disk otherwise - a source checkout that has not been packed, or a file added since
	 * the last build, still works, while a released build never needs the folder at all.
	 * @param p_origin receives the name to report in #line directives and compile errors:
	 *        the short archive name when it came from the archive, else the path read. */
	bool ReadShaderSource(const std::string& p_path, std::string& p_text, std::string* p_origin = nullptr);
}
