#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "ChunkContainer.h"
#include "Flattener.h"
#include "MaterialBuilder.h"
#include "SibGenerator.h"
#include "UibGenerator.h"
#include "ShaderGenerator.h"

/** The package's chunk classes, one per tag: each owns the data for its chunk and writes its
 * payload in Flatten(), the way filamat's eiff/*Chunk classes do. CompileSource only builds
 * these objects and pushes them into a ChunkContainer; no serialization field order lives
 * there. */
namespace Moon::Matc
{
	/** A chunk whose payload is one scalar: the version, the material name, the source hash. */
	template<typename T>
	class SimpleFieldChunk final: public Chunk
	{
	public:
		SimpleFieldChunk(Moon::Material::ChunkType p_type, T p_value)
			: Chunk(p_type), m_value(std::move(p_value)) {}

		void Flatten(Flattener& p_flattener) const override
		{
			if constexpr (std::is_same_v<T, uint32_t>)
			{
				p_flattener.WriteUint32(m_value);
			}
			else if constexpr (std::is_same_v<T, uint64_t>)
			{
				p_flattener.WriteUint64(m_value);
			}
			else if constexpr (std::is_same_v<T, std::string>)
			{
				p_flattener.WriteString(m_value);
			}
			else
			{
				static_assert(sizeof(T) == 0, "unsupported SimpleFieldChunk type");
			}
		}

	private:
		T m_value;
	};

	/** DIC_TEXT: the unique line table. */
	class DictionaryTextChunk final: public Chunk
	{
	public:
		explicit DictionaryTextChunk(std::vector<std::string> p_lines)
			: Chunk(Moon::Material::ChunkType::DictionaryText), m_lines(std::move(p_lines)) {}

		void Flatten(Flattener& p_flattener) const override;

	private:
		std::vector<std::string> m_lines;
	};

	/** MON_STAG: the shared stage texts, encoded as ids into the line dictionary. */
	class MaterialStageTextChunk final: public Chunk
	{
	public:
		using StageLines = std::vector<std::pair<std::string, std::vector<uint32_t>>>;

		explicit MaterialStageTextChunk(StageLines p_stages)
			: Chunk(Moon::Material::ChunkType::MoonStageText), m_stages(std::move(p_stages)) {}

		void Flatten(Flattener& p_flattener) const override;

	private:
		StageLines m_stages;
	};

	/** MAT_GLSL: one #define block per (pass, features, stage) key. */
	class MaterialGlslChunk final: public Chunk
	{
	public:
		using Entry = std::pair<std::string, std::string>;

		explicit MaterialGlslChunk(std::vector<Entry> p_entries)
			: Chunk(Moon::Material::ChunkType::MaterialGlsl), m_entries(std::move(p_entries)) {}

		void Flatten(Flattener& p_flattener) const override;

	private:
		std::vector<Entry> m_entries;
	};

	/** MON_VARI: the pass + feature set of every compiled variant. */
	class MaterialVariantTableChunk final: public Chunk
	{
	public:
		explicit MaterialVariantTableChunk(std::vector<PassVariant> p_variants)
			: Chunk(Moon::Material::ChunkType::MoonVariantTable), m_variants(std::move(p_variants)) {}

		void Flatten(Flattener& p_flattener) const override;

	private:
		std::vector<PassVariant> m_variants;
	};

	/** MON_FEAT: the features the material declared. */
	class MaterialFeatureSetChunk final: public Chunk
	{
	public:
		explicit MaterialFeatureSetChunk(std::vector<std::string> p_features)
			: Chunk(Moon::Material::ChunkType::MoonFeatureSet), m_features(std::move(p_features)) {}

		void Flatten(Flattener& p_flattener) const override;

	private:
		std::vector<std::string> m_features;
	};

	/** MAT_PROP: the reflected parameters. */
	class MaterialPropertiesChunk final: public Chunk
	{
	public:
		explicit MaterialPropertiesChunk(std::vector<Parameter> p_parameters)
			: Chunk(Moon::Material::ChunkType::MaterialProperties), m_parameters(std::move(p_parameters)) {}

		void Flatten(Flattener& p_flattener) const override;

	private:
		std::vector<Parameter> m_parameters;
	};

	/** MAT_ATTR: the vertex attributes the material reads. */
	class MaterialAttributeInfoChunk final: public Chunk
	{
	public:
		explicit MaterialAttributeInfoChunk(std::vector<VertexAttribute> p_attributes)
			: Chunk(Moon::Material::ChunkType::MaterialAttributeInfo), m_attributes(std::move(p_attributes)) {}

		void Flatten(Flattener& p_flattener) const override;

	private:
		std::vector<VertexAttribute> m_attributes;
	};

	/** MAT_UIB: filament's MaterialUniformInterfaceBlockChunk, with the Moon block size and
	 * the std140 offset/stride fields this runtime writes by. */
	class MaterialUniformInterfaceBlockChunk final: public Chunk
	{
	public:
		explicit MaterialUniformInterfaceBlockChunk(UboBlockInfo p_ubo)
			: Chunk(Moon::Material::ChunkType::MaterialUib), m_ubo(std::move(p_ubo)) {}

		void Flatten(Flattener& p_flattener) const override;

	private:
		UboBlockInfo m_ubo;
	};

	/** MAT_SIB: filament's MaterialSamplerInterfaceBlockChunk. */
	class MaterialSamplerInterfaceBlockChunk final: public Chunk
	{
	public:
		explicit MaterialSamplerInterfaceBlockChunk(SamplerBlockInfo p_samplers)
			: Chunk(Moon::Material::ChunkType::MaterialSib), m_samplers(std::move(p_samplers)) {}

		void Flatten(Flattener& p_flattener) const override;

	private:
		SamplerBlockInfo m_samplers;
	};

	/** MON_STAT: the material block's render state. */
	class MaterialStateChunk final: public Chunk
	{
	public:
		explicit MaterialStateChunk(std::map<std::string, std::string> p_state)
			: Chunk(Moon::Material::ChunkType::MoonMaterialState), m_state(std::move(p_state)) {}

		void Flatten(Flattener& p_flattener) const override;

	private:
		std::map<std::string, std::string> m_state;
	};
}
