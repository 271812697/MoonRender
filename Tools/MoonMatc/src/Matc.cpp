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
#include "Flattener.h"
#include "ChunkContainer.h"
#include "MaterialChunks.h"
#include "Includes.h"
#include "MaterialBuilder.h"
#include "MaterialJson.h"
#include "LineDictionary.h"
#include "MaterialParser.h"
#include "MaterialVariants.h"
#include "ShaderGenerator.h"
#include "ParametersProcessor.h"
#include "SibGenerator.h"
#include "TextUtils.h"
#include "UibGenerator.h"

#include <Rendering/Resources/MaterialChunkType.h>
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
	// The package: an integer-tagged chunk tree, like filabridge's MaterialChunkType.h
	// ---------------------------------------------------------------------------------

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

	/** The CRC32 Filament appends as its last chunk. */
	uint32_t Crc32(const uint8_t* p_data, size_t p_size)
	{
		uint32_t crc = 0xFFFFFFFFu;
		for (size_t i = 0; i < p_size; ++i)
		{
			crc ^= p_data[i];
			for (int bit = 0; bit < 8; ++bit)
			{
				crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
			}
		}
		return ~crc;
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
		// A file with no material block is not a material yet (the shaders are migrated one at
		// a time): copy it through byte for byte, which is exactly what the pipeline did before
		// MoonMatc existed. A material block that failed to parse is a real error and must not
		// be passed through as if the file were a legacy shader.
		if (error == "no material { } block")
		{
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

		result.success = false;
		result.error = error;
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

	// Which MaterialInputs fields the material's own fragment code writes. The generator
	// turns that into the MATERIAL_HAS_* defines and the MaterialInputs struct.
	material.writtenFields = ShaderGenerator::WrittenInputFields(material.fragmentBody);
	ShaderGenerator generator(material, material.writtenFields, nameForLines, includeDirs, includeResolver);

	// The shared stage texts, one per stage the material has, with the variant defines left as
	// a placeholder. Everything else in a stage is the same for every variant, so this is the
	// difference between "192 copies of the same 64 KB" and one.
	std::map<std::string, std::string> sharedStages;
	{
		const PassVariant none;
		for (const Stage stage : { Stage::Vertex, Stage::Fragment })
		{
			std::string text = generator.Build(stage, none, true);
			if (text.empty())
			{
				result.error = generator.Error();
				return result;
			}
			sharedStages[StageName(stage)] = std::move(text);
		}
		if (!material.geometryBody.empty())
		{
			std::string text = generator.Build(Stage::Geometry, none, true);
			if (text.empty())
			{
				result.error = generator.Error();
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

		blobs[prefix + "vertex"] = generator.VariantDefines(variant, Stage::Vertex, sharedStages.at("vertex"));
		blobs[prefix + "fragment"] = generator.VariantDefines(variant, Stage::Fragment, sharedStages.at("fragment"));
		blobOrder.push_back(prefix + "vertex");
		blobOrder.push_back(prefix + "fragment");

		// A geometry stage is written by the material and only exists for the shaders that
		// expand primitives, so it is emitted when the block is there and left out otherwise.
		// It is also left out for the variants whose guard feature is off: the shaders that
		// use one open it with `#if defined(WITH_EDGE)`, and the old pipeline skipped the
		// stage there rather than running a pass-through.
		if (!material.geometryBody.empty() && generator.WantsGeometryStage(variant))
		{
			blobs[prefix + "geometry"] = generator.VariantDefines(variant, Stage::Geometry, sharedStages.at("geometry"));
			blobOrder.push_back(prefix + "geometry");
		}
	}

	// Reflection: the parameters this material exposes and the vertex attributes its stages
	// read, in the shape a panel (or a material editor) can read straight away. This is the
	// whole point of doing it offline: no glGetActiveUniform at run time, and the defaults the
	// material file declared come along with it.
	const std::vector<VertexAttribute> attributes = ShaderGenerator::CollectVertexAttributes(sharedStages.at("vertex"));

	result.json = BuildMaterialJson(material, variants, attributes, material.writtenFields, nameForLines);
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

	// The package itself: a chunk tree. CompileSource only decides which chunks to push; each
	// chunk class in MaterialChunks.h owns its payload's field order.
	std::vector<uint8_t> package;
	Flattener flattener(package);
	ChunkContainer container;

	container.Push<SimpleFieldChunk<uint32_t>>(Moon::Material::ChunkType::MaterialVersion, 3);
	container.Push<SimpleFieldChunk<std::string>>(Moon::Material::ChunkType::MaterialName, material.name);
	container.Push<SimpleFieldChunk<uint64_t>>(Moon::Material::ChunkType::MaterialCacheId, HashOf(text));

	// DIC_TEXT / MON_STAG: the line table and the shared stage texts encoded against it. Each
	// stage keeps kDefinesPlaceholder where the variant's #define lines belong, so a 192-variant
	// material does not carry 192 copies of the same source; the table then collapses the lines
	// every stage shares (the prelude). Decoding is exact: a stage is the join of its lines.
	LineDictionary dictionary;
	std::vector<std::pair<std::string, std::vector<uint32_t>>> stageLines;
	for (const auto& [stage, stageText] : sharedStages)
	{
		stageLines.emplace_back(stage, dictionary.Encode(stageText));
	}
	container.Push<DictionaryTextChunk>(dictionary.Lines());
	container.Push<MaterialStageTextChunk>(std::move(stageLines));

	// MAT_GLSL: the per-variant defines - one string key and one string payload per entry.
	{
		std::vector<MaterialGlslChunk::Entry> entries;
		entries.reserve(blobOrder.size());
		for (const std::string& key : blobOrder)
		{
			entries.emplace_back(key, blobs.at(key));
		}
		container.Push<MaterialGlslChunk>(std::move(entries));
	}

	// MON_VARI: the variant list, so the runtime can reject a combination the build never made.
	container.Push<MaterialVariantTableChunk>(variants);

	// MON_FEAT: the features the material declared, so the designer's toggles do not depend on
	// which subset this package happened to compile.
	container.Push<MaterialFeatureSetChunk>(material.features);

	// MAT_PROP: the material's properties as a panel needs them.
	container.Push<MaterialPropertiesChunk>(material.parameters);

	// MAT_ATTR: the vertex attributes the material reads - location, GLSL type, name.
	container.Push<MaterialAttributeInfoChunk>(attributes);

	// MAT_UIB / MAT_SIB: the PerMaterial descriptor halves.
	container.Push<MaterialUniformInterfaceBlockChunk>(BuildUniformBlock(material));
	container.Push<MaterialSamplerInterfaceBlockChunk>(CollectSamplerBlock(material));

	// MON_STAT: the material state (blending, culling, ...) the material block declared.
	container.Push<MaterialStateChunk>(material.state);

	container.Flatten(flattener);

	// MAT_CRC: filament appends the CRC32 of everything before this chunk as the last chunk.
	const uint32_t crc = Crc32(package.data(), package.size());
	flattener.WriteUint64(static_cast<uint64_t>(Moon::Material::ChunkType::MaterialCrc32));
	flattener.WriteUint32(static_cast<uint32_t>(sizeof(crc)));
	flattener.WriteUint32(crc);

	result.package.assign(reinterpret_cast<const char*>(package.data()), package.size());
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
