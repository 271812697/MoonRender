#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include <Rendering/Data/FeatureSet.h>
#include <Rendering/Data/MaterialDefinition.h>
#include <Rendering/HAL/ShaderProgram.h>
#include <Rendering/Settings/EBlendMode.h>

namespace Rendering::Resources
{
	namespace Loaders { class ShaderLoader; }

	/**
	* Represents a shader resource, which wraps a shader program and adds a path to it.
	* Can be seen as a "Shader Asset".
	*/
	class Shader
	{
		friend class Loaders::ShaderLoader;

	public:
		// Shader programs for each feature combination. Shared because keyed packages produce
		// many feature sets whose composed source is identical (all the unused variant bits
		// are pruned): they can point at one linked program instead of compiling per set.
		using FeatureVariants = std::unordered_map<
			Data::FeatureSet,
			std::shared_ptr<HAL::ShaderProgram>,
			Data::FeatureSetHash,
			Data::FeatureSetEqual
		>;

		// Shader programs for each pass for each feature combination
		using Variants = std::unordered_map<
			std::string,
			FeatureVariants
		>;

		/** One parameter the material declares, as the shader package recorded it.
		 *
		 * This is the runtime side of filabridge's material reflection (its UIB and SIB
		 * chunks): the name, the GLSL type, whether it is a texture slot, its array size, and
		 * the initialiser the material declared. Material builds its property set from this
		 * list instead of asking the driver which uniforms stayed active in some variant.
		 *
		 * The package stays the source of truth: a shader built from source has no table, and
		 * its material falls back to the driver's reflection.
		 *
		 * The definition is the one that owns the table now; the alias keeps the call sites that
		 * say Shader::Parameter unchanged. */
		using Parameter = Data::MaterialParameter;

		/** The render state the material block declares - what filament compiles into
		 * MAT_BLEN / MAT_CUMO / MAT_DWRIT. An absent field means the material said nothing about
		 * it, and the instance keeps the engine's default. */
		using State = Data::MaterialState;

		/**
		* Returns the associated shader program for a given feature set
		* @param p_pass (optional) The pass to use. If not provided, the default pass will be selected.
		* @param p_featureSet (optional) The feature set to use. If not provided, the default program will be used.
		*/
		HAL::ShaderProgram& GetVariant(
			std::optional<const std::string_view> p_pass = std::nullopt,
			const Data::FeatureSet& p_featureSet = {}
		);

		/**
		* Returns supported features
		*/
		const Data::FeatureSet& GetFeatures() const;

		/** The features the material declared in its material block, whatever subset this
		 * package compiled. Empty for shaders built from source. */
		const std::vector<std::string>& GetDeclaredFeatures() const;

		void SetDeclaredFeatures(std::vector<std::string>&& p_features);

		/**
		* Returns supported passes
		*/
		const std::unordered_set<std::string>& GetPasses() const;

		/**
		* Return all programs
		*/
		const Variants& GetVariants() const;
		~Shader() = default;
		void SetVariants(Variants&& p_variants);

		/** The parameters the package declared, in declaration order. Empty for a shader built
		 * from source - see HasParameterTable. */
		const std::vector<Parameter>& GetParameters() const;

		/** True when the shader came with a parameter list (a package does; one built from
		 * source does not). It tells Material which description of its properties to trust. */
		bool HasParameterTable() const;

		/** Replaces the parameter list. A hot reload whose package changed its parameters has
		 * to bring the list along, or materials would keep describing the old material. */
		void SetParameters(std::vector<Parameter>&& p_parameters);

		/** True when the parameters live in one std140 uniform block (the per-material
		 * descriptor set: a UBO at binding point 2 plus one texture unit per sampler). Every
		 * packaged shader has one once it has a non-sampler parameter. */
		bool HasParameterBlock() const;

		/** The std140 size of that block, 0 when there is none. */
		uint32_t GetParameterBlockSize() const;

		/** Sets the block size, taken from the package by the loader. */
		void SetParameterBlockSize(uint32_t p_size);

		/** The state the material block declared. Empty fields keep the engine's defaults. */
		const State& GetState() const;

		/** True when the shader came with a material state (a package does; one built from
		 * source does not). */
		bool HasState() const;

		/** True when the variants are Filament-style engine variant keys (their feature names
		 * start with VARIANT_): the renderer then chooses one per draw from the scene/pass state
		 * instead of the material's feature set. */
		bool UsesVariantKeys() const;

		/** Every variant key name this shader was compiled with (the VARIANT_* defines that
		 * appear in at least one variant). The scene's per-frame variant bits are filtered against
		 * it, so a bit the shader never compiles cannot make the lookup miss and land on a variant
		 * without the lighting code. */
		const std::unordered_set<std::string>& GetVariantKeys() const;

		/** Replaces the state, for a hot reload whose material block changed. */
		void SetState(State p_state);
	private:
		Shader(
			const std::string p_path,
			Variants&& p_variants,
			std::optional<std::vector<Parameter>> p_parameters = std::nullopt
		);



	public:
		const std::string path;

	private:
		std::unordered_set<std::string> m_passes;
		Data::FeatureSet m_features;
		std::vector<std::string> m_declaredFeatures;
		Variants m_variants;
		std::vector<Parameter> m_parameters;
		bool m_hasParameterTable = false;
		uint32_t m_parameterBlockSize = 0;
		State m_state;
		bool m_hasState = false;
		bool m_usesVariantKeys = false;
		std::unordered_set<std::string> m_variantKeys;
		/** The package's immutable half. The programs stay in m_variants; this is what a
		 * MaterialInstance reads for offsets, bindings and render state. */
	};
}
