#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <ranges>
#include <set>
#include <string_view>
#include <vector>
#include <tracy/Tracy.hpp>
#include <Rendering/Data/Material.h>
#include <Rendering/Data/MaterialBuilder.h>
#include <Rendering/HAL/UniformBuffer.h>
#include <Rendering/HAL/TextureHandle.h>
#include <Rendering/Resources/ShaderPackage.h>
#include <Rendering/Resources/Texture.h>

#include <Tools/Utils/OptRef.h>
#include "Core/Global/ServiceLocator.h"
#include "Core/ResourceManagement/TextureManager.h"

// ---- MaterialInstance: the override-only half of the pair -------------------------------------

std::unique_ptr<Rendering::Data::MaterialInstance> Rendering::Data::Material::CreateInstance()
{
	return std::make_unique<MaterialInstance>(*this, false);
}

Rendering::Data::Material::Material(Rendering::Resources::Shader* p_shader)
	: m_defaultInstance(std::make_unique<MaterialInstance>(*this, true))
{
	SetShader(p_shader);
}

Rendering::Data::Material::~Material() = default;

bool Rendering::Data::Material::HasShader() const
{
	return m_definition != nullptr && m_definition->Shader() != nullptr;
}

Rendering::Resources::Shader* Rendering::Data::Material::GetShader() const
{
	return m_definition != nullptr
		? const_cast<Rendering::Resources::Shader*>(m_definition->Shader())
		: nullptr;
}

void Rendering::Data::Material::SetShader(Rendering::Resources::Shader* p_shader)
{
	// The definition is the immutable half: package reflection, layout, state and the shader
	// its variants were compiled from. Material itself keeps no shader pointer any more.
	m_definition = p_shader != nullptr
		? MaterialBuilder::Build(*p_shader)
		: nullptr;

	// Rebinding to a shader re-creates the default instance from the definition: the package's
	// material block is the authority for blending / culling / depth writing, and whatever the
	// call site (or a .mat's settings) sets afterwards is the override.
	m_defaultInstance->ResetProperties();
	ResetStateToShader();
}

const Rendering::Data::MaterialDefinition* Rendering::Data::Material::Definition() const
{
	return m_definition.get();
}

void Rendering::Data::Material::ResetStateToShader()
{
	// Engine defaults first; the material block then says what this material is.
	m_defaultInstance->SetBlendable(false);
	m_defaultInstance->SetBlendMode(Rendering::Settings::EBlendMode::BLEND_OPAQUE);
	m_defaultInstance->SetBackfaceCulling(true);
	m_defaultInstance->SetFrontfaceCulling(false);
	m_defaultInstance->SetDepthWriting(true);

	if (Rendering::Resources::Shader* shader = GetShader(); shader != nullptr && shader->HasState())
	{
		const Rendering::Resources::Shader::State& state = shader->GetState();

		if (state.blendable.has_value()) { m_defaultInstance->SetBlendable(state.blendable.value()); }
		if (state.blendMode.has_value())
		{
			m_defaultInstance->SetBlendMode(state.blendMode.value());
			// The definition decides how the colour is blended. Whether the material goes
			// through the transparent (depth-peel) queue stays an instance decision: a
			// transparent-by-DSL shader that does not implement the peel test (the grid, the
			// gizmos) must not be peeled just because of its default.
			m_defaultInstance->SetBlendable(
				state.blendMode.value() != Rendering::Settings::EBlendMode::BLEND_OPAQUE);
		}
		if (state.backfaceCulling.has_value()) { m_defaultInstance->SetBackfaceCulling(state.backfaceCulling.value()); }
		if (state.frontfaceCulling.has_value()) { m_defaultInstance->SetFrontfaceCulling(state.frontfaceCulling.value()); }
		if (state.depthWriting.has_value()) { m_defaultInstance->SetDepthWriting(state.depthWriting.value()); }
	}
}

// Note: this function is critical for performance, as it may be called many times during a frame.
// Avoid using any heavy operations or allocations inside this function.
