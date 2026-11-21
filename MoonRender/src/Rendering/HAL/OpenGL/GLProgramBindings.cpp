#include <Rendering/HAL/OpenGL/GLProgramBindings.h>

#include <cstdlib>
#include <iostream>

#include <glad/glad.h>

#include <Rendering/Data/EngineDescriptorSets.h>


void Rendering::HAL::DeclareProgramBindings(
	uint32_t p_programId,
	const Rendering::Data::DescriptorSetInfo& p_bindings)
{
	glUseProgram(p_programId);

	// Per-view / per-renderable sets use the engine's fixed slot table. No map is needed for
	// them, and no material can move their slots.
	const auto setFixedBinding = [p_programId](
		const Rendering::Data::EngineDescriptorSets::Binding& p_binding)
	{
		if (p_binding.kind == Rendering::Data::DescriptorSetLayout::Kind::Texture)
		{
			const GLint location = glGetUniformLocation(p_programId, p_binding.name);
			if (location >= 0)
			{
				glUniform1i(location, static_cast<GLint>(p_binding.slot));
			}
			return;
		}
		const GLuint index = glGetUniformBlockIndex(p_programId, p_binding.name);
		if (index != GL_INVALID_INDEX)
		{
			glUniformBlockBinding(p_programId, index, p_binding.slot);
		}
	};
	for (const auto& binding : Rendering::Data::EngineDescriptorSets::kPerViewBindings)
	{
		setFixedBinding(binding);
	}
	for (const auto& binding : Rendering::Data::EngineDescriptorSets::kPerRenderableBindings)
	{
		setFixedBinding(binding);
	}

	// Only the per-material set is material-specific. Its buffer slots start after the engine's
	// fixed blocks so it can never alias EngineUBO/ReflectionUBO/ObjectUBO; its texture units
	// start after the view's fixed IBL textures.
	GLuint bufferSlot = Rendering::Data::EngineDescriptorSets::kFirstMaterialBufferSlot;
	GLuint textureSlot = Rendering::Data::EngineDescriptorSets::kFirstMaterialTextureSlot;
	for (const Rendering::Data::DescriptorSetLayout::Binding& binding
		: p_bindings[Rendering::Data::kPerMaterialDescriptorSet])
	{
		const bool textureKind =
			binding.kind == Rendering::Data::DescriptorSetLayout::Kind::Texture;
		const uint32_t slot = textureKind ? textureSlot++ : bufferSlot++;
		const uint64_t key =
			(static_cast<uint64_t>(Rendering::Data::kPerMaterialDescriptorSet) << 32) | binding.binding;
		if (std::getenv("MOON_DEBUG_BINDINGS") != nullptr)
		{
			std::cout << "[BindingMap] set " << Rendering::Data::kPerMaterialDescriptorSet
				<< " " << binding.name << " kind " << static_cast<int>(binding.kind)
				<< " binding " << binding.binding << " -> slot " << slot << std::endl;
		}

		if (textureKind)
		{
			const GLint location = glGetUniformLocation(p_programId, binding.name.c_str());
			if (location >= 0)
			{
				glUniform1i(location, static_cast<GLint>(slot));
			}
		}
		else
		{
			const GLuint index = glGetUniformBlockIndex(p_programId, binding.name.c_str());
			const GLuint resolvedIndex = index != GL_INVALID_INDEX
				? index
				: glGetUniformBlockIndex(p_programId, "MaterialParams");
			if (resolvedIndex != GL_INVALID_INDEX)
			{
				glUniformBlockBinding(p_programId, resolvedIndex, slot);
			}
		}
	}
}
