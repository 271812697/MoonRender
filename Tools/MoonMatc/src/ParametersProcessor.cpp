#include "ParametersProcessor.h"

namespace Moon::Matc
{
	namespace
	{
		/** The text of a string value. Numbers, booleans and enums are read as themselves; the
		 * only source text a Parameter keeps is the name / label / group and the GLSL
		 * initialiser, which are strings by definition. */
		std::string TextOf(const JsonishValue& p_value)
		{
			switch (p_value.getType())
			{
			case JsonishValue::Type::String:
				return p_value.toJsonString()->getString();
			case JsonishValue::Type::Number:
				return p_value.toJsonNumber()->getString();
			case JsonishValue::Type::Bool:
				return p_value.toJsonBool()->getBool() ? "true" : "false";
			case JsonishValue::Type::Null:
				return "null";
			default:
				return {};
			}
		}

		void ApplyType(Parameter& p_parameter, const JsonishValue& p_value)
		{
			if (const JsonishString* value = p_value.toJsonString())
			{
				p_parameter.type = ParseParameterType(value->getString());
			}
		}

		void ApplyName(Parameter& p_parameter, const JsonishValue& p_value)
		{
			if (const JsonishString* value = p_value.toJsonString())
			{
				p_parameter.name = value->getString();
			}
		}

		void ApplyCount(Parameter& p_parameter, const JsonishValue& p_value)
		{
			if (const JsonishNumber* number = p_value.toJsonNumber())
			{
				p_parameter.count = static_cast<uint32_t>(number->getFloat());
			}
		}

		void ApplyDefault(Parameter& p_parameter, const JsonishValue& p_value)
		{
			// A GLSL initialiser (`vec4(...)`, `false`, `1.5`): source text, kept as written.
			p_parameter.defaultExpression = TextOf(p_value);
		}

		void ApplyLabel(Parameter& p_parameter, const JsonishValue& p_value)
		{
			// A quoted label arrives without its quotes: JsonishParser stores the string's
			// content, not the source spelling.
			if (const JsonishString* value = p_value.toJsonString())
			{
				p_parameter.label = value->getString();
			}
		}

		void ApplyGroup(Parameter& p_parameter, const JsonishValue& p_value)
		{
			if (const JsonishString* value = p_value.toJsonString())
			{
				p_parameter.group = value->getString();
			}
		}

		void ApplyColor(Parameter& p_parameter, const JsonishValue& p_value)
		{
			if (const JsonishBool* value = p_value.toJsonBool())
			{
				p_parameter.color = value->getBool();
			}
		}

		void ApplyUi(Parameter& p_parameter, const JsonishValue& p_value)
		{
			// "ui : false" hides engine-owned parameters (the shadow matrix, the environment
			// map) from the material designer; anything else leaves them visible.
			if (const JsonishBool* value = p_value.toJsonBool())
			{
				p_parameter.ui = value->getBool();
			}
			else if (const JsonishString* value = p_value.toJsonString())
			{
				p_parameter.ui = value->getString() != "false";
			}
		}

		void ApplyFallback(Parameter& p_parameter, const JsonishValue& p_value)
		{
			// `fallback : flatNormal` on a sampler says which 1x1 texture the runtime binds when
			// the designer assigned no map - see ShaderPackage::SamplerFallback.
			if (const JsonishString* value = p_value.toJsonString())
			{
				p_parameter.fallback = ParseSamplerFallback(value->getString());
			}
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
			{ "parameters", static_cast<PropertyProcessor>(&ParametersProcessor::OnParameters) },
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

	void ParametersProcessor::OnName(const std::string&, const JsonishValue& p_value)
	{
		m_builder->name = TextOf(p_value);
	}

	void ParametersProcessor::OnShadingModel(const std::string&, const JsonishValue& p_value)
	{
		m_builder->shadingModel = TextOf(p_value);
	}

	void ParametersProcessor::OnDomain(const std::string&, const JsonishValue& p_value)
	{
		m_builder->domain = TextOf(p_value);
	}

	void ParametersProcessor::OnCodeFlag(const std::string& p_key, const JsonishValue& p_value)
	{
		const bool enabled = TextOf(p_value) == "true";
		if (p_key == "specularAntiAliasing") { m_builder->specularAntiAliasing = enabled; }
		else if (p_key == "clearCoatIorChange") { m_builder->clearCoatIorChange = enabled; }
	}

	void ParametersProcessor::OnParameters(const std::string&, const JsonishValue& p_value)
	{
		if (const JsonishArray* array = p_value.toJsonArray())
		{
			for (const JsonishValue* element : array->getElements())
			{
				if (element != nullptr && element->getType() == JsonishValue::Type::Object)
				{
					OnParameters(*element->toJsonObject());
				}
			}
			return;
		}

		// A single object is accepted as well; the DSL normally writes an array.
		if (const JsonishObject* object = p_value.toJsonObject())
		{
			OnParameters(*object);
		}
	}

	void ParametersProcessor::OnParameters(const JsonishObject& p_parameter)
	{
		Parameter parameter;
		for (const auto& [field, apply] : Fields())
		{
			if (const JsonishValue* value = p_parameter.getValue(field))
			{
				apply(parameter, *value);
			}
		}

		// min / max / step are not single fields: they only form a range when both ends are
		// numbers, so they are read together rather than through the table above.
		const JsonishValue* minValue = p_parameter.getValue("min");
		const JsonishValue* maxValue = p_parameter.getValue("max");
		const JsonishNumber* minNumber = minValue != nullptr ? minValue->toJsonNumber() : nullptr;
		const JsonishNumber* maxNumber = maxValue != nullptr ? maxValue->toJsonNumber() : nullptr;
		parameter.hasRange = minNumber != nullptr && maxNumber != nullptr;
		if (parameter.hasRange)
		{
			parameter.min = minNumber->getFloat();
			parameter.max = maxNumber->getFloat();
		}

		if (const JsonishValue* stepValue = p_parameter.getValue("step"))
		{
			if (const JsonishNumber* stepNumber = stepValue->toJsonNumber())
			{
				parameter.hasStep = true;
				parameter.step = stepNumber->getFloat();
			}
		}

		if (parameter.type == ParameterType::Unknown)
		{
			if (m_builder->parseError.empty())
			{
				m_builder->parseError = parameter.name.empty()
					? "parameters: entry without a valid 'type'"
					: "parameters: unknown type for '" + parameter.name + "'";
			}
			return;
		}

		if (!parameter.name.empty())
		{
			m_builder->parameters.push_back(std::move(parameter));
		}
	}

	void ParametersProcessor::OnNameList(const std::string& p_key, const JsonishValue& p_value)
	{
		// An explicit `defaultFeatures : [ ]` means "no optional feature", which is different
		// from the key being absent: the variant enumeration reads that.
		if (p_key == "defaultFeatures") { m_builder->hasDefaultFeatures = true; }

		const auto add = [this, &p_key](const JsonishValue& p_item)
		{
			const std::string item = TextOf(p_item);
			if (item.empty()) { return; }
			if (p_key == "features") { m_builder->features.push_back(item); }
			else if (p_key == "defaultFeatures") { m_builder->defaultFeatures.push_back(item); }
			else if (p_key == "variantFilter") { m_builder->variantFilter.push_back(item); }
			else if (p_key == "vertexAttributes") { m_builder->vertexAttributes.push_back(item); }
			else if (p_key == "passes") { m_builder->passes.push_back(item); }
		};

		if (const JsonishArray* array = p_value.toJsonArray())
		{
			for (const JsonishValue* element : array->getElements())
			{
				if (element != nullptr) { add(*element); }
			}
			return;
		}

		add(p_value);
	}

	void ParametersProcessor::ApplyProperty(const std::string& p_key, const JsonishValue& p_value)
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
		m_builder->state[p_key] = TextOf(p_value);
	}

	void ParametersProcessor::ProcessBlock(const JsonishObject& p_object, MaterialBuilder& p_builder)
	{
		m_builder = &p_builder;

		// The object has already been parsed: no key/value text is scanned here, and the
		// handlers below query the fields they own directly.
		for (const auto& [key, value] : p_object.getEntries())
		{
			if (value != nullptr)
			{
				ApplyProperty(key, *value);
			}
		}
	}
}
