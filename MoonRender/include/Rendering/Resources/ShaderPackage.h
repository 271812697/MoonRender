#pragma once
#include <Rendering/Data/DescriptorSetLayout.h>
#include <Rendering/Resources/ChunkContainer.h>
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
	 * Layout is filament's chunk tree: a flat sequence of (uint64 chunk type, uint32 payload
	 * size, payload), with no package header. MaterialVersion is the first chunk; the loader
	 * recognises a package by it, exactly as filaflat's ChunkContainer does. */
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
			std::string type;       // the DSL name the material file declared ("float4")
			std::string glslType;   // what it became ("vec4")
			std::string defaultValue; // the GLSL initialiser it declared, empty when none
			std::string label;      // the designer label ("Base Color"), empty when not declared
			std::string group;      // the designer group ("Base"), empty when not declared
			float min = 0.0f;       // the designer range, valid when hasRange
			float max = 0.0f;
			float step = 0.0f;      // the designer step, valid when hasStep
			uint32_t count = 0;     // array size, 0 when the parameter is not an array
			int32_t offset = -1;    // std140 offset in the material parameter block, -1 = no layout
			uint32_t stride = 0;    // std140 element stride (arrays)
			uint32_t binding = 0;   // texture unit of a sampler
			bool isSampler = false; // a texture slot rather than a plain uniform
			bool hasRange = false;
			bool hasStep = false;
			bool isColor = false;   // force a colour picker rather than N number fields
			bool isUi = true;       // false hides engine-owned parameters from the designer
		};

		/** One vertex attribute the material's stages read, as the build recorded it - the
		 * counterpart of filament's MaterialAttributeInfo chunk. A VAO can be checked against a
		 * material without compiling it. Per material, not per variant: a stage that declares an
		 * attribute inside a feature branch contributes whichever declarations are in the text. */
		struct Attribute
		{
			uint32_t location = 0;
			std::string glslType;
			std::string name;
		};

		/** What a sampler samples when the material instance has no texture in that slot.
		 * Filament binds a default texture for an unassigned sampler as well; the kinds here are
		 * the 1x1 textures the engine ships, and the material picks one with
		 * `fallback : <name>` (see MoonMatc's SamplerFallback). */
		enum class SamplerFallback : uint8_t
		{
			/** sampler2D: white (the neutral multiplier for albedo/metal/roughness/AO);
			 * samplerCube: black, which is what the engine always bound. */
			Default = 0,
			/** (0.5, 0.5, 1.0) - a normal map decodes it to the neutral tangent-space
			 * normal (0, 0, 1): "no perturbation". */
			FlatNormal = 1,
		};

		struct SamplerFallbackEntry
		{
			std::string name;
			SamplerFallback kind = SamplerFallback::Default;
		};

		/** The stage a blob belongs to. Geometry is the engine's own extension: GLSL has a
		 * third stage and the shaders that expand lines and triangle edges use it. */
		enum class Stage
		{
			Vertex,
			Fragment,
			Geometry
		};

		/** True when the blob the archive holds is a package rather than a raw .shader, which
		 * is what a shader that has not been migrated yet still is. */
		static bool IsPackage(const void* p_data, uint32_t p_size);

		/** Parses the chunk tree (MAT_VERS first, MAT_CRC last) and decodes the payloads. */
		ShaderPackage(const void* p_data, uint32_t p_size);

		bool IsValid() const { return m_valid; }
		const std::string& Name() const { return m_name; }
		const std::vector<Variant>& Variants() const { return m_variants; }

		/** The features the material declared (material { features : [...] }), independent of
		 * which ones this package was compiled with. The designer's feature toggles come from
		 * here: a fixed-feature package's variant union only lists the enabled ones. */
		const std::vector<std::string>& DeclaredFeatures() const { return m_declaredFeatures; }

		/** The material's reflection, straight out of the package: name, types, sampler flag,
		 * array size and declared default per parameter, plus the vertex attributes. This is
		 * what a material panel reads instead of asking the driver for active uniforms. */
		const std::vector<Parameter>& Parameters() const { return m_parameters; }

		/** One field of the PerMaterial uniform block (MAT_UIB) - filament's UboBlockInfo. */
		struct UniformField
		{
			std::string name;
			std::string type;      // the GLSL type
			uint32_t offset = 0;
			uint32_t stride = 0;
			uint32_t count = 1;
		};

		/** One descriptor of the PerMaterial sampler block (MAT_SIB) - SamplerBlockInfo. */
		struct SamplerField
		{
			std::string name;
			std::string type;
			uint32_t binding = 0;
			uint8_t fallback = 0;
		};

		/** The PerMaterial layout the build recorded: where each parameter lives in the uniform
		 * block, and which texture unit each sampler owns. The runtime writes a
		 * MaterialInstance's values through these two lists. */
		const std::vector<UniformField>& UniformFields() const { return m_uniformFields; }
		const std::vector<SamplerField>& SamplerFields() const { return m_samplerFields; }
		uint32_t UniformBlockSize() const { return m_uniformBlockSize; }

		/** The PerMaterial descriptor layout the package describes: the uniform block plus one
		 * texture descriptor per sampler. A DescriptorSet built against it cannot bind a texture
		 * into the block's slot. */
		const ::Rendering::Data::DescriptorSetLayout& Layout() const { return m_layout; }
		const std::vector<Attribute>& Attributes() const { return m_attributes; }

		/** One entry per sampler, carrying the 1x1 texture the runtime binds when the instance
		 * assigned none (MAT_SIB's fallback field). */
		const std::vector<SamplerFallbackEntry>& SamplerFallbacks() const { return m_samplerFallbacks; }

		/** The state the `material { }` block declared besides the name, domain and shading model:
		 * `blending`, `culling`, `depthWrite`, ... as the text the material wrote. Filament bakes
		 * the same fields into its own chunks (MAT_BLEN, MAT_CUMO, MAT_DWRIT); the runtime maps
		 * them onto the material's render state. */
		const std::map<std::string, std::string>& States() const { return m_states; }

		/** The std140 size of the per-material parameter block, 0 when the material has none
		 * (every parameter is a sampler, or the package predates the block). */
		uint32_t ParameterBlockSize() const { return m_parameterBlockSize; }

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
			std::string name;
			std::string source;
		};

		const Blob* FindBlob(const Variant& p_variant, Stage p_stage) const;

		bool m_valid = false;
		std::string m_name;
		std::vector<Variant> m_variants;
		std::vector<Parameter> m_parameters;
		std::vector<Attribute> m_attributes;
		std::map<std::string, std::string> m_states;
		uint32_t m_parameterBlockSize = 0;
		std::vector<SamplerFallbackEntry> m_samplerFallbacks;
		std::vector<Blob> m_blobs;
		/** The package's chunk list (filaflat's ChunkContainer). */
		ChunkContainer m_container;
		/** The package's line table (DIC_TEXT): the shared stage texts are stored as ids into
		 * it, so the lines every stage repeats are stored once. */
		std::vector<std::string> m_lines;
		/** The PerMaterial layout (MAT_UIB / MAT_SIB), applied to m_parameters as it is read. */
		std::vector<UniformField> m_uniformFields;
		std::vector<SamplerField> m_samplerFields;
		uint32_t m_uniformBlockSize = 0;
		/** The descriptor layout, built from those two blocks while the package is read. */
		::Rendering::Data::DescriptorSetLayout m_layout;
		/** Stage name ("vertex" / "fragment" / "geometry") -> the stage's whole text, with the
		 * placeholder where the variant's #define lines go. Owned rather than a view: a stage is
		 * reconstructed from the line table when the package is read. Empty for a package
		 * written before the shared-stage chunk existed. */
		std::map<std::string, std::string> m_stageTexts;
		std::vector<std::string> m_declaredFeatures;
	};
}
