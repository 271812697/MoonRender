// MoonMatc - the offline half of the shader pipeline, the counterpart of Filament's matc.
//
// One .shader in, one .ovshader package out. The package holds what the runtime used to work
// out on every launch: the final GLSL of every (pass, variant, stage), the reflection of the
// material's parameters, and the variant list. A file that has not been migrated yet (no
// material { } block) is copied through untouched, so the pipeline can take the shaders one
// at a time.
//
// This file is the library: parsing, variant expansion, code generation and the package.
// main.cpp is the command line over it, and the editor calls CompileSource directly to hot
// reload a material it is editing.
//
// See docs/ShaderPipeline.md for the format and the design it copies.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Matc.h"

#include "FileUtils.h"
#include "Includes.h"
#include "MaterialBuilder.h"
#include "LineDictionary.h"
#include "MaterialParser.h"
#include "MaterialVariants.h"
#include "ShaderGenerator.h"
#include "ParametersProcessor.h"
#include "SibGenerator.h"
#include "TextUtils.h"
#include "UibGenerator.h"

#include <Rendering/Settings/EVariant.h>

namespace Moon::Matc
{
namespace
{
	// ---------------------------------------------------------------------------------
	// Source handling
	// ---------------------------------------------------------------------------------

	bool WriteFile(const std::filesystem::path& p_path, const std::string& p_text)
	{
		std::error_code error;
		if (p_path.has_parent_path())
		{
			std::filesystem::create_directories(p_path.parent_path(), error);
		}
		std::ofstream stream(p_path, std::ios::binary);
		if (!stream)
		{
			std::cerr << "[MoonMatc] cannot write " << p_path << std::endl;
			return false;
		}
		stream.write(p_text.data(), static_cast<std::streamsize>(p_text.size()));
		return static_cast<bool>(stream);
	}

	// Trim / Unquote / ParseFloatText moved to TextUtils.h (shared with the
	// parameter processor).

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

	std::string ToUpper(std::string p_text)
	{
		std::transform(p_text.begin(), p_text.end(), p_text.begin(),
			[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		return p_text;
	}

	// ---------------------------------------------------------------------------------
	// The material file
	// ---------------------------------------------------------------------------------

	// Parameter / Stage / StageName / PassVariant / MaterialBuilder live in
	// MaterialBuilder.h: the parser and the parameter processor share them.





	// ---------------------------------------------------------------------------------
	// The package: 8 byte tags, like filabridge/include/filament/MaterialChunkType.h
	// ---------------------------------------------------------------------------------

	constexpr char kMagic[8] = { 'M', 'O', 'O', 'N', 'S', 'D', 'R', '1' };

	void AppendChunk(std::string& p_out, const char* p_tag, const std::string& p_payload)
	{
		p_out.append(p_tag, 8);
		const uint32_t size = static_cast<uint32_t>(p_payload.size());
		p_out.append(reinterpret_cast<const char*>(&size), sizeof(size));
		p_out.append(p_payload);
	}

	uint64_t HashOf(const std::string& p_text)
	{
		uint64_t hash = 1469598103934665603ull; // FNV-1a
		for (unsigned char c : p_text)
		{
			hash ^= c;
			hash *= 1099511628211ull;
		}
		return hash;
	}
} // anonymous namespace

	/** Build-time diagnostic: the Filament-style variant enumeration this material asks for
	 * (its variantFilter), with the stage filtering applied. The package still stores the
	 * feature-based variants; the runtime switches to VariantKey selection in phase 3.3. */
	void WriteVariantDump(const MaterialFile& p_material, const std::string& p_path)
	{
		struct BitName
		{
			const char* name;
			const char* define;
			Rendering::Settings::EVariantBits bit;
		};
		static constexpr BitName kBits[] = {
			{ "DIRECTIONAL_LIGHTING", "VARIANT_HAS_DIRECTIONAL_LIGHTING", Rendering::Settings::EVariantBits::DIRECTIONAL_LIGHTING },
			{ "DYNAMIC_LIGHTING", "VARIANT_HAS_DYNAMIC_LIGHTING", Rendering::Settings::EVariantBits::DYNAMIC_LIGHTING },
			{ "SHADOWING", "VARIANT_HAS_SHADOWING", Rendering::Settings::EVariantBits::SHADOWING },
			{ "FOG", "VARIANT_HAS_FOG", Rendering::Settings::EVariantBits::FOG },
			{ "PICKING", "VARIANT_HAS_PICKING", Rendering::Settings::EVariantBits::PICKING },
			{ "SSR", "VARIANT_HAS_SSR", Rendering::Settings::EVariantBits::SSR },
			{ "DEPTH", "VARIANT_DEPTH", Rendering::Settings::EVariantBits::DEPTH },
		};

		std::vector<uint32_t> bits;
		std::ostringstream out;
		out << "material: " << p_material.name << "\n";
		out << "variant filter:";
		for (const std::string& name : p_material.variantFilter)
		{
			for (const BitName& entry : kBits)
			{
				if (name == entry.name)
				{
					bits.push_back(static_cast<uint32_t>(entry.bit));
					out << " " << entry.define;
				}
			}
		}
		out << "\n";

		std::vector<uint32_t> fragments;
		std::vector<uint32_t> vertices;
		for (size_t mask = 0; mask < (size_t{ 1 } << bits.size()); ++mask)
		{
			uint32_t key = 0;
			for (size_t i = 0; i < bits.size(); ++i)
			{
				if (mask & (size_t{ 1 } << i)) { key |= bits[i]; }
			}
			if (Rendering::Settings::FilterVariantFragment(key) == key) { fragments.push_back(key); }
			if (Rendering::Settings::FilterVariantVertex(key) == key) { vertices.push_back(key); }
		}

		const auto defines = [&](uint32_t p_key) {
			std::string names;
			for (const BitName& entry : kBits)
			{
				if ((p_key & static_cast<uint32_t>(entry.bit)) != 0)
				{
					names += std::string(" ") + entry.define;
				}
			}
			return names;
		};

		out << "fragment variants: " << fragments.size() << "\n";
		for (uint32_t key : fragments)
		{
			out << "  0x" << std::hex << key << std::dec << ":" << defines(key) << "\n";
		}
		out << "vertex variants: " << vertices.size() << "\n";
		for (uint32_t key : vertices)
		{
			out << "  0x" << std::hex << key << std::dec << ":" << defines(key) << "\n";
		}

		WriteFile(p_path, out.str());
	}
	CompileResult CompileSource(const std::string& p_sourceText, const CompileOptions& p_options)
{
	CompileResult result;

	const std::string& inputPath = p_options.displayName;
	const std::string& outputPath = p_options.outputPath;
	const std::string& dumpGlslDir = p_options.dumpGlslDir;
	const std::string& dumpJsonPath = p_options.dumpJsonPath;
	const std::vector<std::string>& includeDirs = p_options.includeDirs;
	const IncludeResolver& includeResolver = p_options.includeResolver;
	const bool preprocessOnly = p_options.preprocessOnly;

	const std::string& text = p_sourceText;

	MaterialFile material;
	std::string error;
	MaterialParser parser;
	if (!parser.Parse(text, material, error))
	{
		// Not a material file yet (the shaders are migrated one at a time): copy it through
		// byte for byte, which is exactly what the pipeline did before MoonMatc existed.
		result.passthrough = true;
		result.package = text;
		result.success = true;
		if (!outputPath.empty() && !WriteFile(outputPath, text))
		{
			result.success = false;
			result.error = "cannot write " + outputPath;
			return result;
		}
		return result;
	}

	// The material's descriptors: std140 offsets for the parameter block and the sampler units.
	// Done before code generation and before the package is written, so both sides use the same
	// numbers. A lit material first gets the engine globals the surface prelude reads, so they
	// are laid out with the material's own parameters.
	InjectEngineGlobals(material);
	LayoutParameters(material);

	if (!p_options.dumpVariantsPath.empty())
	{
		WriteVariantDump(material, p_options.dumpVariantsPath);
	}

	std::vector<PassVariant> variants = EnumerateVariants(material, p_options);

	const std::filesystem::path fileName = std::filesystem::path(inputPath).filename();
	const std::string nameForLines = fileName.string();

	// Prelude: the buffer and utility headers the engine already has, then the generated
	// MaterialInputs. Everything the shader used to #include by hand.
	std::string commonPrelude;
	for (const std::string& relative : { "Common/Buffers/EngineUBO.glsl", "Common/Utils.glsl" })
	{
		std::string content;
		bool found = false;
		if (includeResolver)
		{
			std::string origin;
			found = includeResolver(relative, content, origin);
		}
		if (!found)
		{
			for (const std::string& dir : includeDirs)
			{
				const std::string candidate = (std::filesystem::path(dir) / relative).string();
				content = ReadFile(candidate, found);
				if (found)
				{
					break;
				}
			}
		}
		if (found)
		{
			commonPrelude += "\n#line 1 \"" + relative + "\"\n" + content + "\n";
		}
	}
	// Which MaterialInputs fields the material's own fragment code writes. This is the input of
	// the MATERIAL_HAS_* defines, and it has to be known before the prelude is built because the
	// struct and its defaults are guarded by them (surface_material_inputs.fs does the same).
	// Kept on the material as well: the generated stages ask whether they need a tangent frame.
	material.writtenFields = WrittenInputFields(material.fragmentBody);
	const std::set<std::string>& writtenInputFields = material.writtenFields;

	commonPrelude += "\n#line 1 \"<material inputs>\"\n"
		+ MaterialInputsPrelude(material.shadingModel, writtenInputFields, material);

	// The shared stage texts, one per stage the material has, with the variant defines left as
	// a placeholder. Everything else in a stage is the same for every variant, so this is the
	// difference between "192 copies of the same 64 KB" and one.
	std::map<std::string, std::string> sharedStages;
	{
		const PassVariant none;
		for (const Stage stage : { Stage::Vertex, Stage::Fragment })
		{
			std::string text = ShaderGenerator::Build(
				stage, material, none, commonPrelude, nameForLines, includeDirs,
				includeResolver, error, true);
			if (text.empty())
			{
				result.error = error;
				return result;
			}
			sharedStages[StageName(stage)] = std::move(text);
		}
		if (!material.geometryBody.empty())
		{
			std::string text = ShaderGenerator::Build(
				Stage::Geometry, material, none, commonPrelude, nameForLines, includeDirs,
				includeResolver, error, true);
			if (text.empty())
			{
				result.error = error;
				return result;
			}
			sharedStages[StageName(Stage::Geometry)] = std::move(text);
		}
	}

	// Entries: "pass/features/stage" -> the variant's #define lines, and the reflection table.
	// The full source of a stage is sharedStages[stage] with those lines spliced in.
	std::map<std::string, std::string> blobs;
	std::vector<std::string> blobOrder;
	for (const PassVariant& variant : variants)
	{
		std::string featureKey;
		for (const std::string& feature : variant.features)
		{
			featureKey += (featureKey.empty() ? "" : "|") + feature;
		}
		const std::string prefix = variant.pass + "/" + featureKey + "/";

		blobs[prefix + "vertex"] = VariantDefines(variant, Stage::Vertex, sharedStages.at("vertex"));
		blobs[prefix + "fragment"] = VariantDefines(variant, Stage::Fragment, sharedStages.at("fragment"));
		blobOrder.push_back(prefix + "vertex");
		blobOrder.push_back(prefix + "fragment");

		// A geometry stage is written by the material and only exists for the shaders that
		// expand primitives, so it is emitted when the block is there and left out otherwise.
		// It is also left out for the variants whose guard feature is off: the shaders that
		// use one open it with `#if defined(WITH_EDGE)`, and the old pipeline skipped the
		// stage there rather than running a pass-through.
		if (!material.geometryBody.empty() && WantsGeometryStage(material.geometryBody, variant))
		{
			blobs[prefix + "geometry"] = VariantDefines(variant, Stage::Geometry, sharedStages.at("geometry"));
			blobOrder.push_back(prefix + "geometry");
		}
	}

	// Reflection: the parameters this material exposes and the vertex attributes its stages
	// read, in the shape a panel (or a material editor) can read straight away. This is the
	// whole point of doing it offline: no glGetActiveUniform at run time, and the defaults the
	// material file declared come along with it.
	const std::vector<VertexAttribute> attributes = CollectVertexAttributes(sharedStages.at("vertex"));

	std::ostringstream json;
	json << "{\n  \"name\": \"" << material.name << "\",\n"
		<< "  \"shadingModel\": \"" << material.shadingModel << "\",\n"
		<< "  \"domain\": \"" << material.domain << "\",\n"
		<< "  \"source\": \"" << nameForLines << "\",\n"
		// The material-level defines, i.e. the MATERIAL_HAS_* set the code below produced.
		// Same information the generated GLSL carries, for the editor and for A/B work.
		<< "  \"defines\": [";
	{
		size_t d = 0;
		const auto emitDefine = [&json, &d](const std::string& p_name, int p_value) {
			json << (d++ == 0 ? "\n" : ",\n")
				<< "    { \"name\": \"" << p_name << "\", \"value\": " << p_value << " }";
		};
		for (const std::string& define : WrittenInputDefines(writtenInputFields))
		{
			emitDefine(std::string("MATERIAL_HAS_") + define, 1);
		}
		const bool lit = (material.shadingModel != "unlit");
		const bool needsTbn = NeedsTangentFrame(writtenInputFields);
		emitDefine("MATERIAL_NEEDS_TBN", needsTbn ? 1 : 0);
		emitDefine("GEOMETRIC_SPECULAR_AA", (material.specularAntiAliasing && lit) ? 1 : 0);
		emitDefine("CLEAR_COAT_IOR_CHANGE", material.clearCoatIorChange ? 1 : 0);
		json << "\n  ],\n";
	}
	json << "  \"variants\": [\n";
	for (size_t i = 0; i < variants.size(); ++i)
	{
		json << "    { \"pass\": \"" << variants[i].pass << "\", \"features\": [";
		size_t f = 0;
		for (const std::string& feature : variants[i].features)
		{
			json << (f++ == 0 ? "\"" : ", \"") << feature << "\"";
		}
		json << "] }" << (i + 1 < variants.size() ? "," : "") << "\n";
	}
	// The PerMaterial descriptor layout: the two structures the package will carry as
	// ChunkUboBlockInfo / ChunkSamplerBlockInfo (filament's UboBlockInfo / SamplerBlockInfo).
	{
		const UboBlockInfo ubo = BuildUniformBlock(material);
		const SamplerBlockInfo samplers = CollectSamplerBlock(material);

		json << "  ],\n  \"ubo\": {\n"
			<< "    \"name\": \"" << ubo.name << "\", \"size\": " << ubo.size << ",\n"
			<< "    \"uniforms\": [\n";
		for (size_t i = 0; i < ubo.uniforms.size(); ++i)
		{
			const UniformInfo& uniform = ubo.uniforms[i];
			json << "      { \"name\": \"" << uniform.name
				<< "\", \"type\": \"" << uniform.type
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
				<< "\", \"type\": \"" << sampler.type
				<< "\", \"binding\": " << sampler.binding
				<< ", \"fallback\": " << static_cast<uint32_t>(sampler.fallback) << " }"
				<< (i + 1 < samplers.samplers.size() ? "," : "") << "\n";
		}
		json << "  ],\n";
	}
	json << "  \"parameters\": [\n";
	for (size_t i = 0; i < material.parameters.size(); ++i)
	{
		const Parameter& parameter = material.parameters[i];
		json << "    { \"name\": \"" << parameter.name
			<< "\", \"type\": \"" << parameter.type
			<< "\", \"glslType\": \"" << GlslType(parameter.type) << "\""
			<< ", \"sampler\": " << (IsSamplerType(parameter.type) ? "true" : "false");
		if (!parameter.count.empty())
		{
			json << ", \"count\": " << parameter.count;
		}
		if (!parameter.defaultValue.empty())
		{
			json << ", \"default\": \"" << parameter.defaultValue << "\"";
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
			if (!parameter.fallback.empty())
			{
				json << ", \"fallback\": \"" << JsonEscape(parameter.fallback) << "\"";
			}
		}
		else
		{
			json << ", \"offset\": " << parameter.offset << ", \"stride\": " << parameter.stride;
		}
		json << " }" << (i + 1 < material.parameters.size() ? "," : "") << "\n";
	}
	json << "  ],\n  \"parameterBlockSize\": " << material.parameterBlockSize << ",\n  \"attributes\": [\n";
	for (size_t i = 0; i < attributes.size(); ++i)
	{
		json << "    { \"location\": " << attributes[i].location
			<< ", \"name\": \"" << attributes[i].name
			<< "\", \"glslType\": \"" << attributes[i].glslType << "\" }"
			<< (i + 1 < attributes.size() ? "," : "") << "\n";
	}
	json << "  ]\n}\n";

	result.json = json.str();
	if (!dumpJsonPath.empty() && !WriteFile(dumpJsonPath, result.json))
	{
		result.error = "cannot write " + dumpJsonPath;
		return result;
	}

	// The full source of a stage: its shared text with this variant's defines spliced in.
	const auto composeStage = [&sharedStages](const std::string& p_stage, const std::string& p_defines) {
		std::string text = sharedStages.at(p_stage);
		const size_t at = text.find(kDefinesPlaceholder);
		if (at != std::string::npos)
		{
			text.replace(at, sizeof(kDefinesPlaceholder) - 1, p_defines);
		}
		return text;
	};

	if (!dumpGlslDir.empty())
	{
		for (const std::string& key : blobOrder)
		{
			std::string fileNameOut = key;
			std::replace(fileNameOut.begin(), fileNameOut.end(), '/', '_');
			std::replace(fileNameOut.begin(), fileNameOut.end(), '|', '-');

			// The last path component is the stage name the shared text is keyed by.
			const std::string stage = key.substr(key.rfind('/') + 1);
			if (!WriteFile(std::filesystem::path(dumpGlslDir) / (fileNameOut + ".glsl"),
					composeStage(stage, blobs[key])))
			{
				result.error = "cannot dump " + fileNameOut + ".glsl into " + dumpGlslDir;
				return result;
			}
		}
	}

	if (preprocessOnly)
	{
		const std::string& first = blobOrder.front();
		result.preprocessed = composeStage(first.substr(first.rfind('/') + 1), blobs[first]);
		result.materialName = material.name;
		result.variantCount = static_cast<uint32_t>(variants.size());
		result.success = true;
		return result;
	}

	// The package itself.
	std::string package;
	package.append(kMagic, sizeof(kMagic));

	// SHDRDICT / SHDRSTAG: the line table and the shared stage texts encoded against it. Each
	// stage keeps kDefinesPlaceholder where the variant's #define lines belong, so a 192-variant
	// material does not carry 192 copies of the same source; the table then collapses the lines
	// every stage shares (the prelude). Decoding is exact: a stage is the join of its lines.
	LineDictionary dictionary;
	std::vector<std::pair<std::string, std::vector<uint32_t>>> stageLines;
	for (const auto& [stage, text] : sharedStages)
	{
		stageLines.emplace_back(stage, dictionary.Encode(text));
	}
	{
		std::string payload;
		const uint32_t count = static_cast<uint32_t>(dictionary.Size());
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const std::string& line : dictionary.Lines())
		{
			const uint32_t size = static_cast<uint32_t>(line.size());
			payload.append(reinterpret_cast<const char*>(&size), sizeof(size));
			payload.append(line);
		}
		AppendChunk(package, "SHDRDICT", payload);
	}
	{
		std::string payload;
		const uint32_t count = static_cast<uint32_t>(stageLines.size());
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const auto& [stage, ids] : stageLines)
		{
			const uint32_t nameSize = static_cast<uint32_t>(stage.size());
			const uint32_t lineCount = static_cast<uint32_t>(ids.size());
			payload.append(reinterpret_cast<const char*>(&nameSize), sizeof(nameSize));
			payload.append(stage);
			payload.append(reinterpret_cast<const char*>(&lineCount), sizeof(lineCount));
			for (const uint32_t id : ids)
			{
				payload.append(reinterpret_cast<const char*>(&id), sizeof(id));
			}
		}
		AppendChunk(package, "SHDRSTAG", payload);
	}

	// SHDRGLSL: the per-variant defines - a count, then (key length, key, size, bytes) per entry.
	{
		std::string payload;
		const uint32_t count = static_cast<uint32_t>(blobOrder.size());
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const std::string& key : blobOrder)
		{
			const uint32_t keySize = static_cast<uint32_t>(key.size());
			const std::string& source = blobs[key];
			const uint32_t size = static_cast<uint32_t>(source.size());
			payload.append(reinterpret_cast<const char*>(&keySize), sizeof(keySize));
			payload.append(key);
			payload.append(reinterpret_cast<const char*>(&size), sizeof(size));
			payload.append(source);
		}
		AppendChunk(package, "SHDRGLSL", payload);
	}

	// SHDRVARI: the variant list, so the runtime can reject a combination the build never made.
	{
		std::string payload;
		const uint32_t count = static_cast<uint32_t>(variants.size());
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const PassVariant& variant : variants)
		{
			const uint32_t passSize = static_cast<uint32_t>(variant.pass.size());
			payload.append(reinterpret_cast<const char*>(&passSize), sizeof(passSize));
			payload.append(variant.pass);
			const uint32_t featureCountOut = static_cast<uint32_t>(variant.features.size());
			payload.append(reinterpret_cast<const char*>(&featureCountOut), sizeof(featureCountOut));
			for (const std::string& feature : variant.features)
			{
				const uint32_t size = static_cast<uint32_t>(feature.size());
				payload.append(reinterpret_cast<const char*>(&size), sizeof(size));
				payload.append(feature);
			}
		}
		AppendChunk(package, "SHDRVARI", payload);
	}

	// SHDRFEAT: the features the material declared, so the designer's toggles do not depend on
	// which subset this package happened to compile.
	{
		std::string payload;
		const uint32_t count = static_cast<uint32_t>(material.features.size());
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const std::string& feature : material.features)
		{
			const uint32_t size = static_cast<uint32_t>(feature.size());
			payload.append(reinterpret_cast<const char*>(&size), sizeof(size));
			payload.append(feature);
		}
		AppendChunk(package, "SHDRFEAT", payload);
	}

	// SHDRPARM: name/type pairs of the material parameters (the binary form of the JSON). Kept as
	// it was for the first readers; SHDRPROP below is the full reflection.
	{
		std::string payload;
		const uint32_t count = static_cast<uint32_t>(material.parameters.size());
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const Parameter& parameter : material.parameters)
		{
			const uint32_t nameSize = static_cast<uint32_t>(parameter.name.size());
			const uint32_t typeSize = static_cast<uint32_t>(parameter.type.size());
			payload.append(reinterpret_cast<const char*>(&nameSize), sizeof(nameSize));
			payload.append(parameter.name);
			payload.append(reinterpret_cast<const char*>(&typeSize), sizeof(typeSize));
			payload.append(parameter.type);
		}
		AppendChunk(package, "SHDRPARM", payload);
	}

	// SHDRPROP: the material's properties as a panel needs them - name, the DSL type it was
	// declared with, the GLSL type it becomes, whether it is a sampler (texture slot) or a plain
	// uniform, its array size, the GLSL initialiser it declared, and the optional designer fields
	// (label, group, range, step, colour, hidden).
	//
	// filament splits this into a UIB (uniform interface block: name, size, type, precision) and
	// a SIB (sampler interface block: name, binding, type, format); we keep plain uniforms and
	// bind textures by name, so there is nothing to lay out and no descriptor set to index - one
	// list with a flag carries the same information.
	{
		std::string payload;
		const uint32_t count = static_cast<uint32_t>(material.parameters.size());
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const Parameter& parameter : material.parameters)
		{
			const std::string glslType = GlslType(parameter.type);
			const uint32_t arrayCount = parameter.count.empty()
				? 0u : static_cast<uint32_t>(std::stoul(parameter.count));
			const uint8_t sampler = IsSamplerType(parameter.type) ? 1 : 0;
			const uint32_t nameSize = static_cast<uint32_t>(parameter.name.size());
			const uint32_t dslSize = static_cast<uint32_t>(parameter.type.size());
			const uint32_t glslSize = static_cast<uint32_t>(glslType.size());
			const uint32_t defaultSize = static_cast<uint32_t>(parameter.defaultValue.size());
			const uint32_t labelSize = static_cast<uint32_t>(parameter.label.size());
			const uint32_t groupSize = static_cast<uint32_t>(parameter.group.size());
			const uint8_t flags =
				(parameter.hasRange ? 0x01 : 0x00) |
				(parameter.hasStep ? 0x02 : 0x00) |
				(parameter.color ? 0x04 : 0x00) |
				(parameter.ui ? 0x00 : 0x08);

			payload.append(reinterpret_cast<const char*>(&nameSize), sizeof(nameSize));
			payload.append(parameter.name);
			payload.append(reinterpret_cast<const char*>(&dslSize), sizeof(dslSize));
			payload.append(parameter.type);
			payload.append(reinterpret_cast<const char*>(&glslSize), sizeof(glslSize));
			payload.append(glslType);
			payload.append(reinterpret_cast<const char*>(&arrayCount), sizeof(arrayCount));
			payload.append(reinterpret_cast<const char*>(&sampler), sizeof(sampler));
			payload.append(reinterpret_cast<const char*>(&defaultSize), sizeof(defaultSize));
			payload.append(parameter.defaultValue);
			payload.append(reinterpret_cast<const char*>(&labelSize), sizeof(labelSize));
			payload.append(parameter.label);
			payload.append(reinterpret_cast<const char*>(&groupSize), sizeof(groupSize));
			payload.append(parameter.group);
			payload.append(reinterpret_cast<const char*>(&parameter.min), sizeof(parameter.min));
			payload.append(reinterpret_cast<const char*>(&parameter.max), sizeof(parameter.max));
			payload.append(reinterpret_cast<const char*>(&parameter.step), sizeof(parameter.step));
			payload.append(reinterpret_cast<const char*>(&flags), sizeof(flags));

			// The descriptor layout: std140 offset/stride for the parameter block, the texture
			// unit for a sampler.
			payload.append(reinterpret_cast<const char*>(&parameter.offset), sizeof(parameter.offset));
			payload.append(reinterpret_cast<const char*>(&parameter.stride), sizeof(parameter.stride));
			payload.append(reinterpret_cast<const char*>(&parameter.binding), sizeof(parameter.binding));
		}
		payload.append(reinterpret_cast<const char*>(&material.parameterBlockSize), sizeof(material.parameterBlockSize));
		AppendChunk(package, "SHDRPROP", payload);
	}

	// SHDRFALL: the sampler fallbacks (`fallback : flatNormal`). One entry per sampler that
	// declares a non-default one. A separate chunk rather than another field inside SHDRPROP,
	// so a package written before this existed still parses: the reader skips unknown chunks.
	// Nothing is written when no sampler asked for one, which keeps every other package
	// byte-identical to before.
	{
		std::vector<const Parameter*> fallbacks;
		for (const Parameter& parameter : material.parameters)
		{
			if (IsSamplerType(parameter.type) && SamplerFallbackKind(parameter.fallback) != 0)
			{
				fallbacks.push_back(&parameter);
			}
		}

		if (!fallbacks.empty())
		{
			std::string payload;
			const uint32_t count = static_cast<uint32_t>(fallbacks.size());
			payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
			for (const Parameter* parameter : fallbacks)
			{
				const uint32_t nameSize = static_cast<uint32_t>(parameter->name.size());
				const uint8_t kind = SamplerFallbackKind(parameter->fallback);
				payload.append(reinterpret_cast<const char*>(&nameSize), sizeof(nameSize));
				payload.append(parameter->name);
				payload.append(reinterpret_cast<const char*>(&kind), sizeof(kind));
			}
			AppendChunk(package, "SHDRFALL", payload);
		}
	}

	// SHDRATTR: the vertex attributes the material reads - location, GLSL type, name. Same idea
	// as filament's MaterialAttributeInfo chunk, so a VAO can be checked against a material
	// without compiling it.
	{
		std::string payload;
		const uint32_t count = static_cast<uint32_t>(attributes.size());
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const VertexAttribute& attribute : attributes)
		{
			const uint32_t nameSize = static_cast<uint32_t>(attribute.name.size());
			const uint32_t typeSize = static_cast<uint32_t>(attribute.glslType.size());
			payload.append(reinterpret_cast<const char*>(&attribute.location), sizeof(attribute.location));
			payload.append(reinterpret_cast<const char*>(&typeSize), sizeof(typeSize));
			payload.append(attribute.glslType);
			payload.append(reinterpret_cast<const char*>(&nameSize), sizeof(nameSize));
			payload.append(attribute.name);
		}
		AppendChunk(package, "SHDRATTR", payload);
	}

	// SHDRUBO / SHDRSMP: the PerMaterial descriptor layout. The uniform block carries every
	// non-sampler parameter with its std140 offset/stride/count, the sampler block one descriptor
	// per sampler (unit + type + fallback) - the two structures a MaterialInstance looks a
	// parameter up in (filament's UboBlockInfo / SamplerBlockInfo).
	{
		const UboBlockInfo ubo = BuildUniformBlock(material);
		std::string payload;
		const uint32_t nameSize = static_cast<uint32_t>(ubo.name.size());
		const uint32_t count = static_cast<uint32_t>(ubo.uniforms.size());
		payload.append(reinterpret_cast<const char*>(&ubo.size), sizeof(ubo.size));
		payload.append(reinterpret_cast<const char*>(&nameSize), sizeof(nameSize));
		payload.append(ubo.name);
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const UniformInfo& uniform : ubo.uniforms)
		{
			const uint32_t uniformNameSize = static_cast<uint32_t>(uniform.name.size());
			const uint32_t typeSize = static_cast<uint32_t>(uniform.type.size());
			payload.append(reinterpret_cast<const char*>(&uniformNameSize), sizeof(uniformNameSize));
			payload.append(uniform.name);
			payload.append(reinterpret_cast<const char*>(&typeSize), sizeof(typeSize));
			payload.append(uniform.type);
			payload.append(reinterpret_cast<const char*>(&uniform.offset), sizeof(uniform.offset));
			payload.append(reinterpret_cast<const char*>(&uniform.stride), sizeof(uniform.stride));
			payload.append(reinterpret_cast<const char*>(&uniform.count), sizeof(uniform.count));
		}
		AppendChunk(package, "SHDRUBO", payload);
	}
	{
		const SamplerBlockInfo samplers = CollectSamplerBlock(material);
		std::string payload;
		const uint32_t nameSize = static_cast<uint32_t>(samplers.name.size());
		const uint32_t count = static_cast<uint32_t>(samplers.samplers.size());
		payload.append(reinterpret_cast<const char*>(&nameSize), sizeof(nameSize));
		payload.append(samplers.name);
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const SamplerInfo& sampler : samplers.samplers)
		{
			const uint32_t samplerNameSize = static_cast<uint32_t>(sampler.name.size());
			const uint32_t typeSize = static_cast<uint32_t>(sampler.type.size());
			payload.append(reinterpret_cast<const char*>(&samplerNameSize), sizeof(samplerNameSize));
			payload.append(sampler.name);
			payload.append(reinterpret_cast<const char*>(&typeSize), sizeof(typeSize));
			payload.append(sampler.type);
			payload.append(reinterpret_cast<const char*>(&sampler.binding), sizeof(sampler.binding));
			payload.append(reinterpret_cast<const char*>(&sampler.fallback), sizeof(sampler.fallback));
		}
		AppendChunk(package, "SHDRSMP", payload);
	}

	// SHDRSTAT: the material state (blending, culling, ...) the material block declared.
	{
		std::string payload;
		const uint32_t count = static_cast<uint32_t>(material.state.size());
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const auto& [key, value] : material.state)
		{
			const uint32_t keySize = static_cast<uint32_t>(key.size());
			const uint32_t valueSize = static_cast<uint32_t>(value.size());
			payload.append(reinterpret_cast<const char*>(&keySize), sizeof(keySize));
			payload.append(key);
			payload.append(reinterpret_cast<const char*>(&valueSize), sizeof(valueSize));
			payload.append(value);
		}
		AppendChunk(package, "SHDRSTAT", payload);
	}

	// SHDRNAME / SHDRUUID: what this package is, and whether it still matches its sources.
	AppendChunk(package, "SHDRNAME", material.name);
	{
		const uint64_t hash = HashOf(text);
		std::string payload(reinterpret_cast<const char*>(&hash), sizeof(hash));
		AppendChunk(package, "SHDRUUID", payload);
	}
	{
		const char version[] = "1";
		AppendChunk(package, "SHDRVERS", version);
	}

	result.package = std::move(package);
	result.materialName = material.name;
	result.variantCount = static_cast<uint32_t>(variants.size());
	result.success = true;

	if (!outputPath.empty() && !WriteFile(outputPath, result.package))
	{
		result.success = false;
		result.error = "cannot write " + outputPath;
	}
	return result;
}

CompileResult CompileFile(const std::string& p_path, CompileOptions p_options)
{
	if (p_options.displayName.empty())
	{
		p_options.displayName = std::filesystem::path(p_path).filename().string();
	}

	bool ok = false;
	const std::string text = ReadFile(p_path, ok);
	if (!ok)
	{
		CompileResult result;
		result.error = "cannot read " + p_path;
		return result;
	}
	return CompileSource(text, p_options);
}

} // namespace Moon::Matc
