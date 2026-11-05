#pragma once
#include <cstdint>

/** The package's chunk identifiers, modelled on filament's
 * libs/filabridge/include/filament/MaterialChunkType.h.
 *
 * A package is a flat sequence of chunks:
 *
 *     uint64 chunkType   (the identifier below, packed from its 8 characters)
 *     uint32 size        (bytes of payload that follow)
 *     uint8  payload[size]
 *
 * The identifiers are integers, not strings: charTo64BitNum packs the eight-character
 * mnemonic into the same uint64 Filament uses. Filament has no package header; readers walk
 * the chunk list until the buffer is exhausted. `MaterialVersion` is written first and is what
 * identifies a package to the loader.
 *
 * The chunks Filament already names are reused where our data has the same meaning (MAT_UIB,
 * MAT_SIB, MAT_GLSL, MAT_ATTR, MAT_PROP, DIC_TEXT, MAT_NAME, MAT_VERS, MAT_UUID, MAT_CRC).
 * The three MON_* chunks cover this engine's extensions: shared stage text (a MoonMatc
 * optimization), the variant table and the generic render-state map. */
namespace Moon::Material
{
	// Pack an eight character string into a 64 bit integer (filament's charTo64bitNum).
	constexpr inline uint64_t CharTo64BitNum(const char p_str[9]) noexcept
	{
		return
			(static_cast<uint64_t>(p_str[0]) << 56)
			| ((static_cast<uint64_t>(p_str[1]) << 48) & 0x00FF000000000000ULL)
			| ((static_cast<uint64_t>(p_str[2]) << 40) & 0x0000FF0000000000ULL)
			| ((static_cast<uint64_t>(p_str[3]) << 32) & 0x000000FF00000000ULL)
			| ((static_cast<uint64_t>(p_str[4]) << 24) & 0x00000000FF000000ULL)
			| ((static_cast<uint64_t>(p_str[5]) << 16) & 0x0000000000FF0000ULL)
			| ((static_cast<uint64_t>(p_str[6]) << 8) & 0x000000000000FF00ULL)
			| (static_cast<uint64_t>(p_str[7]) & 0x00000000000000FFULL);
	}

	enum class ChunkType : uint64_t
	{
		Unknown = CharTo64BitNum("UNKNOWN "),

		MaterialUib = CharTo64BitNum("MAT_UIB "),
		MaterialSib = CharTo64BitNum("MAT_SIB "),
		/** Per-variant shader text entries; in this build the shared stage texts live in
		 * MoonStageText and this chunk carries each variant's #define lines. */
		MaterialGlsl = CharTo64BitNum("MAT_GLSL"),
		MaterialAttributeInfo = CharTo64BitNum("MAT_ATTR"),
		MaterialProperties = CharTo64BitNum("MAT_PROP"),

		DictionaryText = CharTo64BitNum("DIC_TEXT"),

		MaterialName = CharTo64BitNum("MAT_NAME"),
		MaterialVersion = CharTo64BitNum("MAT_VERS"),
		MaterialCacheId = CharTo64BitNum("MAT_UUID"),
		MaterialCrc32 = CharTo64BitNum("MAT_CRC "),

		/** Our extension: the line-dictionary ids of each shared stage text. */
		MoonStageText = CharTo64BitNum("MON_STAG"),
		/** Our extension: pass + feature set of every compiled variant. */
		MoonVariantTable = CharTo64BitNum("MON_VARI"),
		/** Our extension: the features the material declared. */
		MoonFeatureSet = CharTo64BitNum("MON_FEAT"),
		/** Our extension: render state the material block declared, key/value text. */
		MoonMaterialState = CharTo64BitNum("MON_STAT"),
	};

	inline bool IsKnownChunkType(uint64_t p_type) noexcept
	{
		switch (static_cast<ChunkType>(p_type))
		{
		case ChunkType::MaterialUib:
		case ChunkType::MaterialSib:
		case ChunkType::MaterialGlsl:
		case ChunkType::MaterialAttributeInfo:
		case ChunkType::MaterialProperties:
		case ChunkType::DictionaryText:
		case ChunkType::MaterialName:
		case ChunkType::MaterialVersion:
		case ChunkType::MaterialCacheId:
		case ChunkType::MaterialCrc32:
		case ChunkType::MoonStageText:
		case ChunkType::MoonVariantTable:
		case ChunkType::MoonFeatureSet:
		case ChunkType::MoonMaterialState:
			return true;
		default:
			return false;
		}
	}
}
