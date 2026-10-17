#pragma once
#include <cstdint>
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
		 * combination (i.e. one the build did not make). */
		std::string_view Source(const Variant& p_variant, bool p_vertex) const;

	private:
		struct Blob
		{
			std::string_view name;
			std::string_view source;
		};

		const Blob* FindBlob(const Variant& p_variant, bool p_vertex) const;

		bool m_valid = false;
		std::string m_name;
		std::vector<Variant> m_variants;
		std::vector<Parameter> m_parameters;
		std::vector<Blob> m_blobs;
	};
}
