#include "ParametersProcessor.h"

#include <cctype>
#include <sstream>

#include "TextUtils.h"

namespace Moon::Matc
{
	namespace
	{
		void ApplyType(Parameter& p_parameter, const std::string& p_value)
		{
			p_parameter.type = p_value;
		}

		void ApplyName(Parameter& p_parameter, const std::string& p_value)
		{
			p_parameter.name = p_value;
		}

		void ApplyCount(Parameter& p_parameter, const std::string& p_value)
		{
			p_parameter.count = p_value;
		}

		void ApplyDefault(Parameter& p_parameter, const std::string& p_value)
		{
			p_parameter.defaultValue = p_value;
		}

		void ApplyLabel(Parameter& p_parameter, const std::string& p_value)
		{
			p_parameter.label = Unquote(p_value);
		}

		void ApplyGroup(Parameter& p_parameter, const std::string& p_value)
		{
			p_parameter.group = Unquote(p_value);
		}

		void ApplyColor(Parameter& p_parameter, const std::string& p_value)
		{
			p_parameter.color = p_value == "true";
		}

		void ApplyUi(Parameter& p_parameter, const std::string& p_value)
		{
			// "ui : false" hides engine-owned parameters (the shadow matrix, the environment
			// map) from the material designer; anything else leaves them visible.
			p_parameter.ui = p_value != "false";
		}

		void ApplyFallback(Parameter& p_parameter, const std::string& p_value)
		{
			// `fallback : flatNormal` on a sampler says which 1x1 texture the runtime binds when
			// the designer assigned no map - see ShaderPackage::SamplerFallback.
			p_parameter.fallback = p_value;
		}
	}

	const std::map<std::string, ParametersProcessor::PropertyProcessor>& ParametersProcessor::Properties()
	{
		static const std::map<std::string, PropertyProcessor> properties = {
			{ "name", &ParametersProcessor::OnName },
			{ "shadingModel", &ParametersProcessor::OnShadingModel },
			{ "domain", &ParametersProcessor::OnDomain },
			{ "specularAntiAliasing", &ParametersProcessor::OnCodeFlag },
			{ "clearCoatIorChange", &ParametersProcessor::OnCodeFlag },
			{ "parameters", &ParametersProcessor::OnParameters },
			{ "features", &ParametersProcessor::OnNameList },
			{ "defaultFeatures", &ParametersProcessor::OnNameList },
			{ "variantFilter", &ParametersProcessor::OnNameList },
			{ "vertexAttributes", &ParametersProcessor::OnNameList },
			{ "passes", &ParametersProcessor::OnNameList },
		};
		return properties;
	}

	const std::vector<std::pair<const char*, ParametersProcessor::FieldProcessor>>&
	ParametersProcessor::Fields()
	{
		static const std::vector<std::pair<const char*, FieldProcessor>> fields = {
			{ "type", &ApplyType },
			{ "name", &ApplyName },
			{ "count", &ApplyCount },
			{ "default", &ApplyDefault },
			{ "label", &ApplyLabel },
			{ "group", &ApplyGroup },
			{ "color", &ApplyColor },
			{ "ui", &ApplyUi },
			{ "fallback", &ApplyFallback },
		};
		return fields;
	}

	void ParametersProcessor::OnName(const std::string&, const std::string& p_value)
	{
		m_builder->name = p_value;
	}

	void ParametersProcessor::OnShadingModel(const std::string&, const std::string& p_value)
	{
		m_builder->shadingModel = p_value;
	}

	void ParametersProcessor::OnDomain(const std::string&, const std::string& p_value)
	{
		m_builder->domain = p_value;
	}

	void ParametersProcessor::OnCodeFlag(const std::string& p_key, const std::string& p_value)
	{
		const bool enabled = p_value == "true";
		if (p_key == "specularAntiAliasing") { m_builder->specularAntiAliasing = enabled; }
		else if (p_key == "clearCoatIorChange") { m_builder->clearCoatIorChange = enabled; }
	}

	void ParametersProcessor::OnParameters(const std::string&, const std::string& p_value)
	{
		size_t at = 0;
		while ((at = p_value.find('{', at)) != std::string::npos)
		{
			const size_t end = p_value.find('}', at);
			if (end == std::string::npos) { break; }
			ProcessParameterObject(p_value.substr(at + 1, end - at - 1));
			at = end + 1;
		}
	}

	void ParametersProcessor::OnNameList(const std::string& p_key, const std::string& p_value)
	{
		// An explicit `defaultFeatures : [ ]` means "no optional feature", which is different
		// from the key being absent: the variant enumeration reads that.
		if (p_key == "defaultFeatures") { m_builder->hasDefaultFeatures = true; }

		std::istringstream stream(p_value);
		std::string item;
		while (std::getline(stream, item, ','))
		{
			const std::string trimmed = Trim(item);
			if (trimmed.empty()) { continue; }
			if (p_key == "features") { m_builder->features.push_back(trimmed); }
			else if (p_key == "defaultFeatures") { m_builder->defaultFeatures.push_back(trimmed); }
			else if (p_key == "variantFilter") { m_builder->variantFilter.push_back(trimmed); }
			else if (p_key == "vertexAttributes") { m_builder->vertexAttributes.push_back(trimmed); }
			else if (p_key == "passes") { m_builder->passes.push_back(trimmed); }
		}
	}

	void ParametersProcessor::ApplyProperty(const std::string& p_key, const std::string& p_value)
	{
		const auto& properties = Properties();
		const auto it = properties.find(p_key);
		if (it != properties.end())
		{
			(this->*(it->second))(p_key, p_value);
			return;
		}

		// Everything else the block declares is render state: blending, culling, depthWrite,
		// polygonOffset, ... The package's state chunk carries it to the runtime.
		m_builder->state[p_key] = p_value;
	}

	void ParametersProcessor::ProcessBlock(const std::string& p_body, MaterialBuilder& p_builder)
	{
		m_builder = &p_builder;

		size_t i = 0;
		const auto skipSpace = [&]() { while (i < p_body.size() && std::isspace(static_cast<unsigned char>(p_body[i]))) { ++i; } };
		const auto readName = [&]() {
			skipSpace();
			std::string name;
			while (i < p_body.size() && (std::isalnum(static_cast<unsigned char>(p_body[i])) || p_body[i] == '_' || p_body[i] == '.'))
			{
				name += p_body[i++];
			}
			return name;
		};

		while (i < p_body.size())
		{
			const std::string key = readName();
			skipSpace();
			if (key.empty())
			{
				++i;
				continue;
			}
			if (i >= p_body.size() || p_body[i] != ':')
			{
				continue;
			}
			++i; // ':'
			skipSpace();

			if (i < p_body.size() && p_body[i] == '[')
			{
				// Array: the parameters (objects) or a plain name list (features / passes / ...).
				const size_t close = p_body.find(']', i);
				const std::string array = p_body.substr(i + 1, (close == std::string::npos ? p_body.size() : close) - i - 1);
				i = (close == std::string::npos) ? p_body.size() : close + 1;
				ApplyProperty(key, array);
				continue;
			}

			// Scalar value: up to the next ',' at this level.
			const size_t end = p_body.find(',', i);
			const std::string value = Trim(p_body.substr(i, (end == std::string::npos ? p_body.size() : end) - i));
			i = (end == std::string::npos) ? p_body.size() : end + 1;
			ApplyProperty(key, value);
		}
	}

	void ParametersProcessor::ProcessParameterObject(const std::string& p_object) const
	{
		Parameter parameter;
		for (const auto& [field, apply] : Fields())
		{
			const std::string value = ValueOf(p_object, field);
			if (!value.empty()) { apply(parameter, value); }
		}

		// min / max / step are not single fields: they only form a range when both ends are
		// numbers, so they are read together rather than through the table above.
		parameter.hasRange = ParseFloatText(ValueOf(p_object, "min"), parameter.min)
			&& ParseFloatText(ValueOf(p_object, "max"), parameter.max);
		parameter.hasStep = ParseFloatText(ValueOf(p_object, "step"), parameter.step);

		if (!parameter.name.empty())
		{
			m_builder->parameters.push_back(parameter);
		}
	}

	std::string ParametersProcessor::ValueOf(const std::string& p_object, const std::string& p_field)
	{
		size_t at = p_object.find(p_field);
		while (at != std::string::npos)
		{
			const size_t after = at + p_field.size();
			const size_t colon = p_object.find(':', after);
			const bool wordStart = at == 0
				|| !std::isalnum(static_cast<unsigned char>(p_object[at - 1]))
				&& p_object[at - 1] != '_';
			if (wordStart && colon != std::string::npos && Trim(p_object.substr(after, colon - after)).empty())
			{
				size_t end = colon + 1;
				int depth = 0;
				for (; end < p_object.size(); ++end)
				{
					const char c = p_object[end];
					if (c == '(' || c == '[') { ++depth; }
					else if (c == ')' || c == ']') { --depth; }
					else if (c == ',' && depth == 0) { break; }
				}
				return Trim(p_object.substr(colon + 1, end - colon - 1));
			}
			at = p_object.find(p_field, at + 1);
		}
		return {};
	}
}
