#pragma once
#include <functional>
#include <Rendering/Resources/Shader.h>

namespace Rendering::Resources::Loaders
{

	class ShaderLoader
	{
	public:
		struct LoggingSettings
		{
			bool summary : 1;
			bool linkingErrors : 1;
			bool linkingSuccess : 1;
			bool compilationErrors : 1;
			bool compilationSuccess : 1;
		};

		using FilePathParserCallback = std::function<std::string(const std::string&)>;

		ShaderLoader() = delete;

		static LoggingSettings GetLoggingSettings();

		static void SetLoggingSettings(LoggingSettings p_settings);

		static Shader* Create(const std::string& p_filePath, FilePathParserCallback p_pathParser = nullptr);
		static Shader* CreateFromSource(const std::string& p_vertexShader, const std::string& p_fragmentShader, const std::string& p_geomertyShader = "");
		static Shader* CreateFromSource(const std::string& p_sourceShader, FilePathParserCallback p_pathParser=nullptr);

		/** Builds a shader straight from a package in memory (the bytes MoonMatc emits), which
		 * is what the material editor needs: it compiles the source being edited into a package
		 * and hot swaps the result without touching the archive or the disk.
		 *
		 * Returns nullptr when the package has no variant the driver accepted, with the
		 * driver's messages - they carry the .ovfx lines, thanks to the #line directives - in
		 * p_error when it is given. */
		static Shader* CreateFromPackage(const void* p_data, uint32_t p_size,
			const std::string& p_name, std::string* p_error = nullptr);

		static void	Recompile(Shader& p_shader, const std::string& p_filePath, FilePathParserCallback p_pathParser = nullptr);
		static bool Destroy(Shader*& p_shader);
	};
}
