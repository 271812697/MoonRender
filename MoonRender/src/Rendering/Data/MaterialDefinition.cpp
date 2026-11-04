#include <Rendering/Data/MaterialDefinition.h>

#include <map>
#include <algorithm>
#include <utility>

#include <Rendering/Data/MaterialParser.h>
#include <Rendering/Data/Material.h>

#include <Rendering/Settings/EBindingPoint.h>

namespace Rendering::Data
{
	namespace
	{
		/** The material block's state as the runtime's optional overrides. The package keeps the
		 * values as the material's text ("back", "transparent", "false"); the runtime works in
		 * booleans. Leaving a field unset means the material did not declare it. */
		MaterialState StateFromPackage(const MaterialParser& p_package)
		{
			MaterialState state;
			const std::map<std::string, std::string>& states = p_package.States();

			if (const auto it = states.find("blending"); it != states.end())
			{
				// filament's names: opaque | transparent | add | multiply | screen | fade.
				// "masked" clips in the shader and blends like opaque, so it lands on OPAQUE too.
				using Rendering::Settings::EBlendMode;
				state.blendable = it->second != "opaque";
				state.blendMode =
					it->second == "transparent" ? EBlendMode::BLEND_TRANSPARENT :
					it->second == "add" ? EBlendMode::BLEND_ADD :
					it->second == "multiply" ? EBlendMode::BLEND_MULTIPLY :
					it->second == "screen" ? EBlendMode::BLEND_SCREEN :
					it->second == "fade" ? EBlendMode::BLEND_FADE :
					EBlendMode::BLEND_OPAQUE;
			}
			if (const auto it = states.find("culling"); it != states.end())
			{
				state.backfaceCulling = it->second == "back";
				state.frontfaceCulling = it->second == "front";
			}
			if (const auto it = states.find("depthWrite"); it != states.end())
			{
				state.depthWriting = it->second == "true";
			}

			return state;
		}
	}

	std::shared_ptr<const MaterialDefinition> MaterialDefinition::FromParser(
		std::shared_ptr<const MaterialParser> p_parser)
	{
		if (p_parser == nullptr || !p_parser->IsValid())
		{
			return nullptr;
		}
		std::shared_ptr<const MaterialDefinition> definition =
			BuildFromParser(*p_parser);
		if (definition != nullptr)
		{
			const_cast<MaterialDefinition*>(definition.get())->m_parser = std::move(p_parser);
		}
		return definition;
	}

	std::shared_ptr<const MaterialDefinition> MaterialDefinition::BuildFromParser(
		const MaterialParser& p_package)
	{
		// Private constructor: use new/delete through the shared_ptr, the class is immutable.
		std::shared_ptr<MaterialDefinition> definition{ new MaterialDefinition() };

		definition->m_name = p_package.Name();
		definition->m_parameterBlockSize = p_package.ParameterBlockSize();
		definition->m_uniformFields.reserve(p_package.UniformFields().size());
		for (const MaterialParser::UniformField& field : p_package.UniformFields())
		{
			definition->m_uniformFields.push_back(UniformField{
				field.name, field.type, field.offset, field.stride, field.count });
		}

		definition->m_samplerFields.reserve(p_package.SamplerFields().size());
		for (const MaterialParser::SamplerField& field : p_package.SamplerFields())
		{
			definition->m_samplerFields.push_back(SamplerField{
				field.name, field.type, field.binding, field.fallback });
		}
		definition->m_layout = p_package.Layout();
		definition->m_state = StateFromPackage(p_package);
		definition->m_hasState = true;
		definition->m_hasParameterTable = true;
		definition->m_declaredFeatures = p_package.DeclaredFeatures();

		const auto fallbackOf = [&p_package](const std::string& p_name) -> uint8_t
		{
			for (const MaterialParser::SamplerFallbackEntry& entry
				: p_package.SamplerFallbacks())
			{
				if (entry.name == p_name)
				{
					return static_cast<uint8_t>(entry.kind);
				}
			}
			return static_cast<uint8_t>(MaterialParser::SamplerFallback::Default);
		};

		definition->m_parameters.reserve(p_package.Parameters().size());
		for (const MaterialParser::Parameter& parameter : p_package.Parameters())
		{
			Parameter resolved;
			resolved.name = parameter.name;
			resolved.type = parameter.glslType;
			resolved.defaultValue = parameter.defaultValue;
			resolved.label = parameter.label;
			resolved.group = parameter.group;
			resolved.min = parameter.min;
			resolved.max = parameter.max;
			resolved.step = parameter.step;
			resolved.count = parameter.count;
			resolved.offset = parameter.offset;
			resolved.stride = parameter.stride;
			resolved.binding = parameter.binding;
			resolved.fallback = parameter.isSampler ? fallbackOf(parameter.name) : uint8_t{ 0 };
			resolved.isSampler = parameter.isSampler;
			resolved.hasRange = parameter.hasRange;
			resolved.hasStep = parameter.hasStep;
			resolved.isColor = parameter.isColor;
			resolved.isUi = parameter.isUi;
			definition->m_parameters.push_back(std::move(resolved));
		}

		// A package written before the SHDRUBO / SHDRSMP chunks (or one whose chunks did not
		// survive) still has the per-parameter offset/binding in SHDRPROP. Derive the two block
		// tables from that, so the old package keeps working through the same descriptor set.
		if (definition->m_layout.IsEmpty()
			&& definition->m_uniformFields.empty()
			&& definition->m_samplerFields.empty())
		{
			for (const Parameter& parameter : definition->m_parameters)
			{
				if (parameter.isSampler)
				{
					definition->m_samplerFields.push_back(SamplerField{
						parameter.name, parameter.type, parameter.binding, parameter.fallback });
					definition->m_layout.Add(parameter.name, DescriptorSetLayout::Kind::Texture,
						parameter.binding);
				}
				else if (parameter.offset >= 0)
				{
					definition->m_uniformFields.push_back(UniformField{
						parameter.name, parameter.type, static_cast<uint32_t>(parameter.offset),
						parameter.stride, parameter.count > 0 ? parameter.count : 1 });
				}
			}
			if (definition->m_parameterBlockSize > 0)
			{
				definition->m_layout.Add("MaterialParams", DescriptorSetLayout::Kind::UniformBuffer,
					Rendering::Settings::kMaterialParameterBinding);
			}
		}

		// Normalize the per-material set: UBO at binding 0 and samplers at 1..N. The package's
		// original GLSL bindings are irrelevant once the program binding map assigns slots; a
		// set cannot have two descriptors at the same array index.
		{
			DescriptorSetLayout normalized;
			if (definition->m_parameterBlockSize > 0)
			{
				normalized.Add("MaterialParams", DescriptorSetLayout::Kind::UniformBuffer, 0);
			}
			uint32_t samplerBinding = 1;
			for (SamplerField& field : definition->m_samplerFields)
			{
				field.binding = samplerBinding;
				normalized.Add(field.name, DescriptorSetLayout::Kind::Texture, samplerBinding++);
			}
			definition->m_layout = std::move(normalized);

			// Keep the parameter table's sampler bindings in sync with the normalized set.
			for (Parameter& parameter : definition->m_parameters)
			{
				if (!parameter.isSampler)
				{
					continue;
				}
				const auto sampler = std::ranges::find_if(definition->m_samplerFields,
					[&](const SamplerField& p_field) { return p_field.name == parameter.name; });
				if (sampler != definition->m_samplerFields.end())
				{
					parameter.binding = sampler->binding;
				}
			}
		}

		// The complete shader-side binding table (filament's Program::DescriptorSetInfo). Sets
		// 0 and 1 are the engine's fixed per-view / per-renderable blocks; set 2 is the material
		// reflection built above.
		definition->m_programBindings[0] = {
			{ "EngineUBO", DescriptorSetLayout::Kind::UniformBuffer,
				Rendering::Settings::kEngineUBOBinding },
			{ "ReflectionUBO", DescriptorSetLayout::Kind::UniformBuffer,
				Rendering::Settings::kReflectionUBOBinding },
		};
		definition->m_programBindings[1] = {
			{ "ObjectUBO", DescriptorSetLayout::Kind::UniformBuffer,
				Rendering::Settings::kObjectUBOBinding },
		};
		definition->m_programBindings[2] = definition->m_layout.Bindings();

		return definition;
	}

	std::shared_ptr<const MaterialDefinition> MaterialDefinition::FromMaterial(
		const Material& p_material)
	{
		std::shared_ptr<MaterialDefinition> definition{ new MaterialDefinition() };

		definition->m_name = p_material.path;
		definition->m_parameters = p_material.GetParameters();
		definition->m_parameterBlockSize = p_material.GetParameterBlockSize();
		definition->m_hasParameterTable = p_material.HasParameterTable();
		definition->m_hasState = p_material.HasState();
		if (definition->m_hasState)
		{
			definition->m_state = p_material.GetState();
		}
		definition->m_declaredFeatures = p_material.GetDeclaredFeatures();

		// A shader built from source has no package block: its reflection lives in the
		// program's uniforms and the loose upload path reads those. The definition is still
		// useful for the parameter list, but it has no layout for the block.
		for (const Parameter& parameter : definition->m_parameters)
		{
			if (parameter.isSampler)
			{
				definition->m_samplerFields.push_back(SamplerField{
					parameter.name, parameter.type, parameter.binding, parameter.fallback });
				definition->m_layout.Add(parameter.name, DescriptorSetLayout::Kind::Texture,
					parameter.binding);
			}
			else if (parameter.offset >= 0)
			{
				definition->m_uniformFields.push_back(UniformField{
					parameter.name, parameter.type, static_cast<uint32_t>(parameter.offset),
					parameter.stride, parameter.count > 0 ? parameter.count : 1 });
			}
		}

		if (definition->m_parameterBlockSize > 0)
		{
			definition->m_layout.Add("MaterialParams", DescriptorSetLayout::Kind::UniformBuffer,
				Rendering::Settings::kMaterialParameterBinding);
		}

		definition->m_programBindings[0] = {
			{ "EngineUBO", DescriptorSetLayout::Kind::UniformBuffer,
				Rendering::Settings::kEngineUBOBinding },
			{ "ReflectionUBO", DescriptorSetLayout::Kind::UniformBuffer,
				Rendering::Settings::kReflectionUBOBinding },
		};
		definition->m_programBindings[1] = {
			{ "ObjectUBO", DescriptorSetLayout::Kind::UniformBuffer,
				Rendering::Settings::kObjectUBOBinding },
		};
		definition->m_programBindings[2] = definition->m_layout.Bindings();

		return definition;
	}

	const MaterialDefinition::SamplerField* MaterialDefinition::FindSampler(const std::string& p_name) const
	{
		for (const SamplerField& field : m_samplerFields)
		{
			if (field.name == p_name)
			{
				return &field;
			}
		}
		return nullptr;
	}

	const MaterialDefinition::UniformField* MaterialDefinition::FindUniform(const std::string& p_name) const
	{
		for (const UniformField& field : m_uniformFields)
		{
			if (field.name == p_name)
			{
				return &field;
			}
		}
		return nullptr;
	}
}
