#include "MaterialJson.h"

#include <sstream>

#include "SibGenerator.h"
#include "UibGenerator.h"

namespace Moon::Matc
{
	namespace
	{
		std::string JsonEscape(const std::string& p_text)
		{
			std::string out;
			out.reserve(p_text.size());
			for (const char c : p_text)
			{
				if (c == '"' || c == '\\')
				{
					out += '\\';
				}
				out += c;
			}
			return out;
		}
	}

	std::string BuildMaterialJson(
		const MaterialBuilder& p_material,
		const std::vector<PassVariant>& p_variants,
		const std::vector<VertexAttribute>& p_attributes,
		const std::set<std::string>& p_writtenFields,
		const std::string& p_sourceName)
	{
		std::ostringstream json;
		json << "{\n  \"name\": \"" << p_material.name << "\",\n"
			<< "  \"shadingModel\": \"" << p_material.shadingModel << "\",\n"
			<< "  \"domain\": \"" << p_material.domain << "\",\n"
			<< "  \"source\": \"" << p_sourceName << "\",\n"
			// The material-level defines, i.e. the MATERIAL_HAS_* set the code below produced.
			// Same information the generated GLSL carries, for the editor and for A/B work.
			<< "  \"defines\": [";
		{
			size_t d = 0;
			const auto emitDefine = [&json, &d](const std::string& p_name, int p_value) {
				json << (d++ == 0 ? "\n" : ",\n")
					<< "    { \"name\": \"" << p_name << "\", \"value\": " << p_value << " }";
			};
			for (const std::string& define : ShaderGenerator::WrittenInputDefines(p_writtenFields))
			{
				emitDefine(std::string("MATERIAL_HAS_") + define, 1);
			}
			const bool lit = (p_material.shadingModel != "unlit");
			const bool needsTbn = ShaderGenerator::NeedsTangentFrame(p_writtenFields);
			emitDefine("MATERIAL_NEEDS_TBN", needsTbn ? 1 : 0);
			emitDefine("GEOMETRIC_SPECULAR_AA", (p_material.specularAntiAliasing && lit) ? 1 : 0);
			emitDefine("CLEAR_COAT_IOR_CHANGE", p_material.clearCoatIorChange ? 1 : 0);
			json << "\n  ],\n";
		}

		json << "  \"variants\": [\n";
		for (size_t i = 0; i < p_variants.size(); ++i)
		{
			json << "    { \"pass\": \"" << p_variants[i].pass << "\", \"features\": [";
			size_t f = 0;
			for (const std::string& feature : p_variants[i].features)
			{
				json << (f++ == 0 ? "\"" : ", \"") << feature << "\"";
			}
			json << "] }" << (i + 1 < p_variants.size() ? "," : "") << "\n";
		}

		// The PerMaterial descriptor layout: the two structures MAT_UIB / MAT_SIB carry
		// (filament's UboBlockInfo / SamplerBlockInfo).
		{
			const UboBlockInfo ubo = BuildUniformBlock(p_material);
			const SamplerBlockInfo samplers = CollectSamplerBlock(p_material);

			json << "  ],\n  \"ubo\": {\n"
				<< "    \"name\": \"" << ubo.name << "\", \"size\": " << ubo.size << ",\n"
				<< "    \"uniforms\": [\n";
			for (size_t i = 0; i < ubo.uniforms.size(); ++i)
			{
				const UniformInfo& uniform = ubo.uniforms[i];
				json << "      { \"name\": \"" << uniform.name
					<< "\", \"type\": \"" << ParameterTypeGlsl(uniform.type)
					<< "\", \"offset\": " << uniform.offset
					<< ", \"stride\": " << uniform.stride
					<< ", \"count\": " << uniform.count << " }"
					<< (i + 1 < ubo.uniforms.size() ? "," : "") << "\n";
			}
			json << "    ]\n  },\n  \"samplers\": [\n";
			for (size_t i = 0; i < samplers.samplers.size(); ++i)
			{
				const SamplerInfo& sampler = samplers.samplers[i];
				json << "    { \"name\": \"" << sampler.name
					<< "\", \"type\": \"" << ParameterTypeName(sampler.type)
					<< "\", \"binding\": " << sampler.binding
					<< ", \"fallback\": " << static_cast<uint32_t>(sampler.fallback) << " }"
					<< (i + 1 < samplers.samplers.size() ? "," : "") << "\n";
			}
			json << "  ],\n";
		}

		json << "  \"parameters\": [\n";
		for (size_t i = 0; i < p_material.parameters.size(); ++i)
		{
			const Parameter& parameter = p_material.parameters[i];
			json << "    { \"name\": \"" << parameter.name
				<< "\", \"type\": \"" << ParameterTypeName(parameter.type)
				<< "\", \"glslType\": \"" << ParameterTypeGlsl(parameter.type) << "\""
				<< ", \"sampler\": " << (IsSamplerType(parameter.type) ? "true" : "false");
			if (parameter.count > 0)
			{
				json << ", \"count\": " << parameter.count;
			}
			if (!parameter.defaultExpression.empty())
			{
				json << ", \"default\": \"" << parameter.defaultExpression << "\"";
			}
			if (!parameter.label.empty())
			{
				json << ", \"label\": \"" << JsonEscape(parameter.label) << "\"";
			}
			if (!parameter.group.empty())
			{
				json << ", \"group\": \"" << JsonEscape(parameter.group) << "\"";
			}
			if (parameter.hasRange)
			{
				json << ", \"min\": " << parameter.min << ", \"max\": " << parameter.max;
			}
			if (parameter.hasStep)
			{
				json << ", \"step\": " << parameter.step;
			}
			if (parameter.color)
			{
				json << ", \"color\": true";
			}
			if (!parameter.ui)
			{
				json << ", \"ui\": false";
			}
			if (IsSamplerType(parameter.type))
			{
				json << ", \"binding\": " << parameter.binding;
				if (parameter.fallback != SamplerFallback::Default)
				{
					json << ", \"fallback\": \"" << SamplerFallbackName(parameter.fallback) << "\"";
				}
			}
			else
			{
				json << ", \"offset\": " << parameter.offset << ", \"stride\": " << parameter.stride;
			}
			json << " }" << (i + 1 < p_material.parameters.size() ? "," : "") << "\n";
		}

		json << "  ],\n  \"parameterBlockSize\": " << p_material.parameterBlockSize
			<< ",\n  \"attributes\": [\n";
		for (size_t i = 0; i < p_attributes.size(); ++i)
		{
			json << "    { \"location\": " << p_attributes[i].location
				<< ", \"name\": \"" << p_attributes[i].name
				<< "\", \"glslType\": \"" << p_attributes[i].glslType << "\" }"
				<< (i + 1 < p_attributes.size() ? "," : "") << "\n";
		}
		json << "  ]\n}\n";

		return json.str();
	}
}
