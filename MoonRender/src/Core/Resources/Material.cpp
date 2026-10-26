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

	Serializer::SerializeBoolean(p_doc, settingsNode, "support_orthographic", SupportsOrthographic());
	Serializer::SerializeBoolean(p_doc, settingsNode, "support_perspective", SupportsPerspective());
	Serializer::SerializeBoolean(p_doc, settingsNode, "blendable", IsBlendable());
	Serializer::SerializeString(p_doc, settingsNode, "blending", BlendModeName(GetBlendMode()));
	// Queue membership, not just a blend factor: the depth-peel pass draws these drawables, so
	// the instance has to remember whether it belongs there.
	Serializer::SerializeBoolean(p_doc, settingsNode, "transparent", IsTransparent());
	Serializer::SerializeBoolean(p_doc, settingsNode, "backface_culling", HasBackfaceCulling());
	Serializer::SerializeBoolean(p_doc, settingsNode, "frontface_culling", HasFrontfaceCulling());
	Serializer::SerializeBoolean(p_doc, settingsNode, "depth_test", HasDepthTest());
	Serializer::SerializeBoolean(p_doc, settingsNode, "depth_writing", HasDepthWriting());
	Serializer::SerializeBoolean(p_doc, settingsNode, "color_writing", HasColorWriting());
	Serializer::SerializeBoolean(p_doc, settingsNode, "cast_shadows", IsShadowCaster());
	Serializer::SerializeBoolean(p_doc, settingsNode, "receive_shadows", IsShadowReceiver());
	Serializer::SerializeBoolean(p_doc, settingsNode, "captured_by_reflection_probes", IsCapturedByReflectionProbes());
	Serializer::SerializeBoolean(p_doc, settingsNode, "receive_reflections", IsReflectionReceiver());
	Serializer::SerializeBoolean(p_doc, settingsNode, "user_interface", IsUserInterface());
	Serializer::SerializeInt(p_doc, settingsNode, "gpu_instances", GetGPUInstances());
	Serializer::SerializeInt(p_doc, settingsNode, "draw_order", GetDrawOrder());
	// The two a shape's material needs but a .mat could not express: the CAD faces push
	// themselves back in depth (polygon offset, slope scaled) and the CAD edges are wide lines.
	Serializer::SerializeBoolean(p_doc, settingsNode, "polygon_offset", HasPolygonOffsetFill());
	Serializer::SerializeFloat(p_doc, settingsNode, "line_width", GetLineWidth());

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
		const auto readBool = [&](const char* p_name, bool p_current, auto&& p_apply)
		{
			bool value = p_current;
			Serializer::DeserializeBoolean(p_doc, settingsNode, p_name, value);
			p_apply(value);
		};
		const auto readInt = [&](const char* p_name, int p_current, auto&& p_apply)
		{
			int value = p_current;
			Serializer::DeserializeInt(p_doc, settingsNode, p_name, value);
			p_apply(value);
		};

		// The values are read through the getters (which fall back to the shader's material block)
		// and written back through the setters, so a field the file omits keeps the definition's
		// value instead of being reset to zero.
		readBool("support_orthographic", SupportsOrthographic(), [this](bool v) { SetOrthographicSupport(v); });
		readBool("support_perspective", SupportsPerspective(), [this](bool v) { SetPerspectiveSupport(v); });
		readBool("blendable", IsBlendable(), [this](bool v) { SetBlendable(v); });
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
		readBool("transparent", IsTransparent(), [this](bool v) { SetTransparent(v); });
		readBool("backface_culling", HasBackfaceCulling(), [this](bool v) { SetBackfaceCulling(v); });
		readBool("frontface_culling", HasFrontfaceCulling(), [this](bool v) { SetFrontfaceCulling(v); });
		readBool("depth_test", HasDepthTest(), [this](bool v) { SetDepthTest(v); });
		readBool("depth_writing", HasDepthWriting(), [this](bool v) { SetDepthWriting(v); });
		readBool("color_writing", HasColorWriting(), [this](bool v) { SetColorWriting(v); });
		readBool("cast_shadows", IsShadowCaster(), [this](bool v) { SetCastShadows(v); });
		readBool("receive_shadows", IsShadowReceiver(), [this](bool v) { SetReceiveShadows(v); });
		readBool("captured_by_reflection_probes", IsCapturedByReflectionProbes(), [this](bool v) { SetCapturedByReflectionProbes(v); });
		readBool("receive_reflections", IsReflectionReceiver(), [this](bool v) { SetReceiveReflections(v); });
		readBool("user_interface", IsUserInterface(), [this](bool v) { SetUserInterface(v); });
		readInt("gpu_instances", GetGPUInstances(), [this](int v) { SetGPUInstances(v); });
		readInt("draw_order", GetDrawOrder(), [this](int v) { SetDrawOrder(v); });
		readBool("polygon_offset", HasPolygonOffsetFill(), [this](bool v) { SetPolygonOffsetFill(v); });
		{
			float value = GetLineWidth();
			Serializer::DeserializeFloat(p_doc, settingsNode, "line_width", value);
			SetLineWidth(value);
		}
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
