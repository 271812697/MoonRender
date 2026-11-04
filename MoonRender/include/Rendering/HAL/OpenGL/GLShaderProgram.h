#pragma once
#include <vector>
#include <unordered_map>
#include <Rendering/Data/ProgramDescriptorBindings.h>
#include <Rendering/HAL/Common/TShaderProgram.h>
#include <Rendering/HAL/OpenGL/GLShaderStage.h>

namespace Rendering::HAL
{
	struct GLShaderProgramContext
	{
		const uint32_t id;
		std::unordered_map<std::string, Settings::UniformInfo> uniforms;
		std::unordered_map<std::string, uint32_t> uniformsLocationCache;
		std::vector<std::reference_wrapper<const GLShaderStage>> attachedShaders;
		/** (set << 32 | binding) -> descriptor slot. Built lazily on the first use of the
		 * program, exactly like filament's OpenGLProgram binding map. */
		std::unordered_map<uint64_t, uint32_t> descriptorSlots;
		bool descriptorSlotsInitialized = false;

		uint32_t GetUniformLocation(std::string_view p_name);
		void InitializeDescriptorBindings(
			uint32_t p_programId,
			const Rendering::Data::DescriptorSetInfo& p_bindings);
		uint32_t GetDescriptorSlot(uint32_t p_set, uint32_t p_binding) const;
	};

	using GLShaderProgram = TShaderProgram<Settings::EGraphicsBackend::OPENGL, GLShaderProgramContext, GLShaderStageContext>;
}
