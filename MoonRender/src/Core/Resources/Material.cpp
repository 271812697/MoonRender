#include <sstream>
#include <tinyxml2.h>

#include <Core/Resources/Material.h>

namespace
{
	const char* BlendModeName(Rendering::Settings::EBlendMode p_mode)
	{
		using Rendering::Settings::EBlendMode;
		switch (p_mode)
		{
		case EBlendMode::BLEND_TRANSPARENT: return "transparent";
		case EBlendMode::BLEND_ADD: return "add";
		case EBlendMode::BLEND_MULTIPLY: return "multiply";
		case EBlendMode::BLEND_SCREEN: return "screen";
		case EBlendMode::BLEND_FADE: return "fade";
		default: return "opaque";
		}
	}

	Rendering::Settings::EBlendMode BlendModeFromName(const std::string& p_name)
	{
		using Rendering::Settings::EBlendMode;
		if (p_name == "transparent") { return EBlendMode::BLEND_TRANSPARENT; }
		if (p_name == "add") { return EBlendMode::BLEND_ADD; }
		if (p_name == "multiply") { return EBlendMode::BLEND_MULTIPLY; }
		if (p_name == "screen") { return EBlendMode::BLEND_SCREEN; }
		if (p_name == "fade") { return EBlendMode::BLEND_FADE; }
		return EBlendMode::BLEND_OPAQUE;
	}
}

void Core::Resources::Material::OnSerialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node)
{
	using namespace Core::Helpers;
	using namespace Rendering::Settings;
	using namespace Maths;

	Serializer::SerializeShader(p_doc, p_node, "shader", m_shader);

	tinyxml2::XMLNode* settingsNode = p_doc.NewElement("settings");
	p_node->InsertEndChild(settingsNode);

	Serializer::SerializeBoolean(p_doc, settingsNode, "support_orthographic", m_supportOrthographic);
	Serializer::SerializeBoolean(p_doc, settingsNode, "support_perspective", m_supportPerspective);
	Serializer::SerializeBoolean(p_doc, settingsNode, "blendable", m_blendable);
	Serializer::SerializeString(p_doc, settingsNode, "blending", BlendModeName(m_blendMode));
	// Queue membership, not just a blend factor: the depth-peel pass draws these drawables, so
	// the instance has to remember whether it belongs there.
	Serializer::SerializeBoolean(p_doc, settingsNode, "transparent", m_transparent);
	Serializer::SerializeBoolean(p_doc, settingsNode, "backface_culling", m_backfaceCulling);
	Serializer::SerializeBoolean(p_doc, settingsNode, "frontface_culling", m_frontfaceCulling);
	Serializer::SerializeBoolean(p_doc, settingsNode, "depth_test", m_depthTest);
	Serializer::SerializeBoolean(p_doc, settingsNode, "depth_writing", m_depthWriting);
	Serializer::SerializeBoolean(p_doc, settingsNode, "color_writing", m_colorWriting);
	Serializer::SerializeBoolean(p_doc, settingsNode, "cast_shadows", m_castShadows);
	Serializer::SerializeBoolean(p_doc, settingsNode, "receive_shadows", m_receiveShadows);
	Serializer::SerializeBoolean(p_doc, settingsNode, "captured_by_reflection_probes", m_capturedByReflectionProbes);
	Serializer::SerializeBoolean(p_doc, settingsNode, "receive_reflections", m_receiveReflections);
	Serializer::SerializeBoolean(p_doc, settingsNode, "user_interface", m_userInterface);
	Serializer::SerializeInt(p_doc, settingsNode, "gpu_instances", m_gpuInstances);
	Serializer::SerializeInt(p_doc, settingsNode, "draw_order", m_drawOrder);

	// Create "Uniforms" (Every uniform will be attached to "Uniforms")
	tinyxml2::XMLNode* uniformsNode = p_doc.NewElement("uniforms");
	p_node->InsertEndChild(uniformsNode);

	const auto program = GetVariant();

	// If the material has no valid program for the current feature set, we skip serialization of properties.
	if (!program)
	{
		return;
	}

	// Block members are not in the program's reflected uniform map (they have no individual
	// location); the shader's parameter table is what says the material has them.
	const auto isDeclaredParameter = [this](const std::string& p_name) {
		if (m_shader == nullptr)
		{
			return false;
		}
		for (const Rendering::Resources::Shader::Parameter& parameter : m_shader->GetParameters())
		{
			if (parameter.name == p_name || p_name.starts_with(parameter.name + "["))
			{
				return true;
			}
		}
		return false;
	};

	for (const auto& [name, prop] : m_properties)
	{
		// Skip serialization of this property if neither the parameter table nor the current
		// program knows it.
		if (!isDeclaredParameter(name) && !program->GetUniformInfo(name))
		{
			continue;
		}

		// Skip serialization of this property if it is marked as single-use
		if (prop.singleUse)
		{
			continue;
		}

		auto& value = prop.value;
		tinyxml2::XMLNode* uniform = p_doc.NewElement("uniform");
		uniformsNode->InsertEndChild(uniform);

		Serializer::SerializeString(p_doc, uniform, "name", name);

		auto visitor = [&](auto&& arg) {
			using T = std::decay_t<decltype(arg)>;
			using enum EUniformType;

			if constexpr (std::same_as<T, bool>)
			{
				Serializer::SerializeInt(p_doc, uniform, "value", arg);
			}
			else if constexpr (std::same_as<T, int>)
			{
				Serializer::SerializeInt(p_doc, uniform, "value", arg);
			}
			else if constexpr (std::same_as<T, float>)
			{
				Serializer::SerializeFloat(p_doc, uniform, "value", arg);
			}
			else if constexpr (std::same_as<T, FVector2>)
			{
				Serializer::SerializeVec2(p_doc, uniform, "value", arg);
			}
			else if constexpr (std::same_as<T, FVector3>)
			{
				Serializer::SerializeVec3(p_doc, uniform, "value", arg);
			}
			else if constexpr (std::same_as<T, FVector4>)
			{
				Serializer::SerializeVec4(p_doc, uniform, "value", arg);
			}
			else if constexpr (std::same_as<T, Rendering::Resources::Texture*>)
			{
				Serializer::SerializeTexture(p_doc, uniform, "value", arg);
			}
			// No need to handle TextureHandle* here as it's not serializable (only texture assets are)
		};

		std::visit(visitor, value);
	}

	std::string features;

	for (auto it = m_features.begin(); it != m_features.end(); ++it)
	{
		features += *it;

		if (std::next(it) != m_features.end())
		{
			features += ",";
		}
	}

	Serializer::SerializeString(p_doc, p_node, "features", features);
}

void Core::Resources::Material::OnDeserialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node)
{
	using namespace Core::Helpers;

	/* The shader comes first: it is the definition, and SetShader resets the definition-owned
	 * state (blending, culling, depth writing) to what its material block declares. The
	 * <settings> below are the instance's overrides and have to win over that. */
	const auto shader = Serializer::DeserializeShader(p_doc, p_node, "shader");

	m_properties.clear();

	/* We verify that the shader is valid (Not null) */
	if (shader)
	{
		/* If the shader is valid, we set it to the material (Modify m_shader pointer + Query + FillUniforms) */
		SetShader(shader);
	}

	tinyxml2::XMLNode* settingsNode = p_node->FirstChildElement("settings");
	
	if (settingsNode)
	{
		Serializer::DeserializeBoolean(p_doc, settingsNode, "support_orthographic", m_supportOrthographic);
		Serializer::DeserializeBoolean(p_doc, settingsNode, "support_perspective", m_supportPerspective);
		Serializer::DeserializeBoolean(p_doc, settingsNode, "blendable", m_blendable);
		{
			// Newer files carry the blend mode; older ones only had the blendable flag, which
			// already means the standard alpha blend.
			std::string blending;
			Serializer::DeserializeString(p_doc, settingsNode, "blending", blending);
			if (!blending.empty())
			{
				SetBlendMode(BlendModeFromName(blending));
			}
		}
		// Explicit queue membership (absent in older files, which is the non-peeled default).
		Serializer::DeserializeBoolean(p_doc, settingsNode, "transparent", m_transparent);
		Serializer::DeserializeBoolean(p_doc, settingsNode, "backface_culling", m_backfaceCulling);
		Serializer::DeserializeBoolean(p_doc, settingsNode, "frontface_culling", m_frontfaceCulling);
		Serializer::DeserializeBoolean(p_doc, settingsNode, "depth_test", m_depthTest);
		Serializer::DeserializeBoolean(p_doc, settingsNode, "depth_writing", m_depthWriting);
		Serializer::DeserializeBoolean(p_doc, settingsNode, "color_writing", m_colorWriting);
		Serializer::DeserializeBoolean(p_doc, settingsNode, "cast_shadows", m_castShadows);
		Serializer::DeserializeBoolean(p_doc, settingsNode, "receive_shadows", m_receiveShadows);
		Serializer::DeserializeBoolean(p_doc, settingsNode, "captured_by_reflection_probes", m_capturedByReflectionProbes);
		Serializer::DeserializeBoolean(p_doc, settingsNode, "receive_reflections", m_receiveReflections);
		Serializer::DeserializeBoolean(p_doc, settingsNode, "user_interface", m_userInterface);
		Serializer::DeserializeInt(p_doc, settingsNode, "gpu_instances", m_gpuInstances);
		Serializer::DeserializeInt(p_doc, settingsNode, "draw_order", m_drawOrder);
	}

	if (shader)
	{
		const auto uniformsNode = p_node->FirstChildElement("uniforms"); // Access to "Uniforms" (Every uniform will be attached to "Uniforms")		

		if (uniformsNode)
		{
			// Iterate er every <uniform>...</uniform>
			for (auto uniform = uniformsNode->FirstChildElement("uniform"); uniform; uniform = uniform->NextSiblingElement("uniform"))
			{
				if (const auto uniformNameElement = uniform->FirstChildElement("name"))
				{
					const auto propName = uniformNameElement->GetText();

					// Make sure that there is a property with the same name.
					// The type of the property value will be used to determine which type to deserialize.
					// This means that the serialized data doesn't hold the type information, and if the
					// type changes after serialization, it will not be deserialized correctly (skipped).
					if (const auto prop = GetProperty(propName))
					{
						auto visitor = [&](auto&& arg) {
							using T = std::decay_t<decltype(arg)>;
							using namespace Maths;

							if constexpr (std::same_as<T, bool>)
							{
								SetProperty(propName, Serializer::DeserializeBoolean(p_doc, uniform, "value"));
							}
							else if constexpr (std::same_as<T, int>)
							{
								SetProperty(propName, Serializer::DeserializeInt(p_doc, uniform, "value"));
							}
							else if constexpr (std::same_as<T, float>)
							{
								SetProperty(propName, Serializer::DeserializeFloat(p_doc, uniform, "value"));
							}
							else if constexpr (std::same_as<T, FVector2>)
							{
								SetProperty(propName, Serializer::DeserializeVec2(p_doc, uniform, "value"));
							}
							else if constexpr (std::same_as<T, FVector3>)
							{
								SetProperty(propName, Serializer::DeserializeVec3(p_doc, uniform, "value"));
							}
							else if constexpr (std::same_as<T, FVector4>)
							{
								SetProperty(propName, Serializer::DeserializeVec4(p_doc, uniform, "value"));
							}
							else if constexpr (std::same_as<T, Rendering::Resources::Texture*>)
							{
								SetProperty(propName, Serializer::DeserializeTexture(p_doc, uniform, "value"));
							}
							// No need to handle TextureHandle* here as it's not serializable (only texture assets are)
						};

						std::visit(visitor, prop->value);
					}
				}
			}
		}
	}

	m_features.clear();

	const auto features = Serializer::DeserializeString(p_doc, p_node, "features");

	// Parse features (comma-separated)
	std::string feature;
	std::istringstream featureStream(features);
	while (std::getline(featureStream, feature, ','))
	{
		if (!feature.empty())
		{
			AddFeature(feature);
		}
	}
}
