#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Rendering::Resources
{
	/** A shader package, as MoonMatc writes it (the counterpart of filament's .filamat).
	 *
	 * It holds the work the runtime used to do on every launch: every (pass, variant, stage)
	 * already resolved to the final GLSL, the variant list, and the material's parameters.
	 * Loading a shader is then a lookup and a compile, not a parse.
	 *
	 * Layout, modelled on filabridge/include/filament/MaterialChunkType.h: an 8 byte magic,
	 * then chunks of (8 byte tag, uint32 size, payload). */
	class ShaderPackage
	{
	public:
		struct Variant
		{
			std::string pass;
			std::set<std::string> features;
		};

		struct Parameter
		{
			std::string name;
			std::string type;
		};

		/** The stage a blob belongs to. Geometry is the engine's own extension: GLSL has a
		 * third stage and the shaders that expand lines and triangle edges use it. */
		enum class Stage
		{
			Vertex,
			Fragment,
			Geometry
		};

		/** True when the blob the archive holds is a package rather than a raw .ovfx, which
		 * is what a shader that has not been migrated yet still is. */
		static bool IsPackage(const void* p_data, uint32_t p_size);

		/** Walks the chunk table; the payloads are read on demand and never copied. */
		ShaderPackage(const void* p_data, uint32_t p_size);

		bool IsValid() const { return m_valid; }
		const std::string& Name() const { return m_name; }
		const std::vector<Variant>& Variants() const { return m_variants; }
		const std::vector<Parameter>& Parameters() const { return m_parameters; }

		/** The final GLSL of one variant's stage, empty when the package holds no such
		 * combination (i.e. one the build did not make, or no geometry stage at all).
		 *
		 * A package stores each stage's text once and the per-variant #define lines apart
		 * (filament's DictionaryChunk idea), so this is an assembly, not a lookup - it returns
		 * by value for that reason. */
		std::string Source(const Variant& p_variant, Stage p_stage) const;

		/** The same thing spelled the way the first callers had it: true = vertex,
		 * false = fragment. */
		std::string Source(const Variant& p_variant, bool p_vertex) const
		{
			return Source(p_variant, p_vertex ? Stage::Vertex : Stage::Fragment);
		}

	private:
		struct Blob
		{
			std::string_view name;
			std::string_view source;
		};

		const Blob* FindBlob(const Variant& p_variant, Stage p_stage) const;

		bool m_valid = false;
		std::string m_name;
		std::vector<Variant> m_variants;
		std::vector<Parameter> m_parameters;
		std::vector<Blob> m_blobs;
		/** Stage name ("vertex" / "fragment" / "geometry") -> the stage's whole text, with the
		 * placeholder where the variant's #define lines go. Empty for a package written before
		 * the shared-stage chunk existed: then the blobs are whole stages, as they used to be. */
		std::map<std::string, std::string_view> m_stageTexts;
	};

	/** Where the per-variant defines are spliced into a stage's shared text. Kept in one place
	 * because MoonMatc writes it and this parser reads it. */
	constexpr std::string_view kShaderDefinesPlaceholder = "//@MOON_VARIANT_DEFINES@";
}
