#pragma once
#include <Rendering/Settings/EGraphicsBackend.h>
#include <Rendering/Settings/EShaderType.h>
#include <Rendering/Settings/ShaderCompilationResult.h>

namespace Rendering::HAL
{
	/**
	* Represents a part of a shader program that is responsible for a specific stage (vertex, fragment, geometry, etc.).
	*/
	template<Settings::EGraphicsBackend Backend, class Context>
	class TShaderStage final
	{
	public:
		/**
		* Creates a shader stage of the given type.
		* @param p_type
		*/
		TShaderStage(Settings::EShaderType p_type);

		/**
		* Destructor of the shader stage.
		*/
		~TShaderStage();

		/**
		* Uploads the shader source to the graphics backend memory.
		* @param p_source
		*/
		void Upload(const std::string& p_source) const;

		/**
		* Compiles the uploaded shader source.
		* @note Use this method after uploading the shader source.
		* @return The compilation result.
		*/
		Settings::ShaderCompilationResult Compile() const;

		/**
		* Returns the ID of the shader stage.
		*/
		uint32_t GetID() const;

		/**
		* Returns the type of the stage.
		*/
		Settings::EShaderType GetShaderType() const;

		/**
		* The source this stage was given. Kept because the build moves to the side that owns the
		* context (the program's create command carries the sources - see ProgramRequests.h).
		*/
		const std::string& GetSource() const;

	private:
		Context m_context;
	};
}
