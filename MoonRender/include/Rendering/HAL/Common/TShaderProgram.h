#pragma once
#include <unordered_map>

#include <Maths/FMatrix3.h>
#include <Maths/FMatrix4.h>
#include <Maths/FVector2.h>
#include <Maths/FVector3.h>
#include <Maths/FVector4.h>

#include <Rendering/HAL/ShaderStage.h>
#include <Rendering/Data/ProgramDescriptorBindings.h>
#include <Rendering/Settings/EGraphicsBackend.h>
#include <Rendering/Settings/UniformInfo.h>
#include <Rendering/Settings/ShaderLinkingResult.h>

#include <Tools/Utils/OptRef.h>

namespace Rendering::HAL
{
	template<typename T>
	concept SupportedUniformType =
		std::same_as<T, float> ||
		std::same_as<T, int> ||
		std::same_as<T, Maths::FVector2> ||
		std::same_as<T, Maths::FVector3> ||
		std::same_as<T, Maths::FVector4> ||
		std::same_as<T, Maths::FMatrix3> ||
		std::same_as<T, Maths::FMatrix4>;

	/**
	* Represents a shader program, used to link shader stages together.
	*/
	template<Settings::EGraphicsBackend Backend, class ProgramContext, class StageContext>
	class TShaderProgram final
	{
	public:
		/**
		* Creates a shader program.
		*/
		TShaderProgram();

		explicit TShaderProgram(const Rendering::Data::DescriptorSetInfo& p_bindings);

		/**
		* Destroys a shader program.
		*/
		~TShaderProgram();

		/**
		* Attaches a shader stage to the program.
		* @param p_shader
		*/
		void Attach(const TShaderStage<Backend, StageContext>& p_shader);

		/**
		* Detaches a shader stage to the program.
		* @param p_shader
		*/
		void Detach(const TShaderStage<Backend, StageContext>& p_shader);

		/**
		* Detaches all shader stages from the program.
		*/
		void DetachAll();

		/**
		* Links the shader stages together.
		* @return The linking result
		*/
		Settings::ShaderLinkingResult Link();

		/**
		* Binds the program.
		*/
		void Bind();

		/**
		* Unbinds the program.
		*/
		void Unbind() const;

		/**
		* Returns the ID of the program.
		*/
		uint32_t GetID() const;

		/**
		* Sends a uniform value associated with the given name to the GPU.
		* @param p_name
		* @param p_value
		*/
		template<SupportedUniformType T>
		void SetUniform(const std::string& p_name, const T& p_value);

		/**
		* Returns the value of a uniform associated with the given name.
		* @param p_name
		* @param p_value
		*/
		template<SupportedUniformType T>
		T GetUniform(const std::string& p_name);

		/**
		* Returns information about the uniform identified by the given name or std::nullopt if not found.
		* @param p_name
		*/
		Tools::Utils::OptRef<const Settings::UniformInfo> GetUniformInfo(const std::string& p_name) const;

		/**
		* Queries the uniforms from the program and caches them in memory.
		*/
		void QueryUniforms();

		/** Stores the descriptor layout; Bind() resolves it lazily on first use. */
		void SetDescriptorBindings(const Rendering::Data::DescriptorSetInfo& p_bindings)
		{
			m_context.SetDescriptorBindings(p_bindings);
		}

		bool HasDescriptorBindings() const { return m_context.descriptorSlotsInitialized; }

		uint32_t GetDescriptorSlot(uint32_t p_set, uint32_t p_binding) const
		{
			return m_context.GetDescriptorSlot(p_set, p_binding);
		}

		/**
		* Returns the uniforms associated with this program.
		*/
		const std::unordered_map<std::string, Settings::UniformInfo>& GetUniforms() const;

	private:
		ProgramContext m_context;
	};
}
