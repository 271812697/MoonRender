#pragma once
#include <vector>
#include <unordered_map>
#include <Rendering/Backend/ResourceHandle.h>
#include <Rendering/Data/ProgramDescriptorBindings.h>
#include <Rendering/HAL/Common/TShaderProgram.h>
#include <Rendering/HAL/OpenGL/GLShaderStage.h>

namespace Rendering::Backend
{
	struct HwProgram;
}

namespace Rendering::HAL
{
	struct GLShaderProgramContext
	{
		/** The GL program name. 0 when the program is built on the side that owns the context: then
		 * the create-program command performs the link and the name is looked up from `handle`
		 * (Filament's ProgramHandle). */
		uint32_t id = 0;
		/** The front-end reference to the program (allocated in threaded mode, see Link). */
		Rendering::Backend::ResourceHandle<Rendering::Backend::HwProgram> handle;
		std::unordered_map<std::string, Settings::UniformInfo> uniforms;
		std::unordered_map<std::string, uint32_t> uniformsLocationCache;
		std::vector<std::reference_wrapper<const GLShaderStage>> attachedShaders;
		/** Per-material (set 2) binding -> descriptor slot. Sets 0/1 use the engine's fixed
		 * slots and do not enter this map. */
		std::unordered_map<uint64_t, uint32_t> descriptorSlots;
		bool descriptorSlotsInitialized = false;
		/** True once the slots have been declared to the program (glUniformBlockBinding /
		 * glUniform1i). That part is GL, and only the side that owns the context may do it. */
		bool descriptorBindingsDeclared = false;
		/** The layout the program will resolve on first use, like filament's lazy program
		 * token data. Configured once at program creation, never by the material. */
		Rendering::Data::DescriptorSetInfo descriptorBindings;
		bool descriptorBindingsConfigured = false;

		uint32_t GetUniformLocation(std::string_view p_name);
		void SetDescriptorBindings(const Rendering::Data::DescriptorSetInfo& p_bindings);
		/** Fills descriptorSlots from the layout. Pure bookkeeping: the numbers come from the
		 * engine's fixed table and the per-material counters, never from GL. */
		void ComputeDescriptorSlots(const Rendering::Data::DescriptorSetInfo& p_bindings);
		/** Tells the program which slot each block and sampler lives in:
		 * glGetUniformBlockIndex + glUniformBlockBinding, glGetUniformLocation + glUniform1i.
		 * That is GL and it needs the program linked, so it runs where the context lives. */
		void DeclareDescriptorBindings(
			uint32_t p_programId,
			const Rendering::Data::DescriptorSetInfo& p_bindings);
		uint32_t GetDescriptorSlot(uint32_t p_set, uint32_t p_binding) const;
	};

	using GLShaderProgram = TShaderProgram<Settings::EGraphicsBackend::OPENGL, GLShaderProgramContext, GLShaderStageContext>;
}
