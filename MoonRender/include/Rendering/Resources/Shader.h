#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include <Rendering/Data/FeatureSet.h>
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
		// Shader programs for each feature combination
		using FeatureVariants = std::unordered_map<
			Data::FeatureSet,
			std::unique_ptr<HAL::ShaderProgram>,
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
		 * its material falls back to the driver's reflection. */
		struct Parameter
		{
			std::string name;
			std::string type;         // the GLSL type: "float", "vec4", "sampler2D", ...
			std::string defaultValue; // the initialiser the material declared, empty when none
			std::string label;        // the designer label ("Base Color"), empty when not declared
			std::string group;        // the designer group ("Base"), empty when not declared
			float min = 0.0f;         // the designer range, valid when hasRange
			float max = 0.0f;
			float step = 0.0f;        // the designer step, valid when hasStep
			uint32_t count = 0;       // array size, 0 when the parameter is not an array
			int32_t offset = -1;      // std140 offset in the material parameter block, -1 = no layout
			uint32_t stride = 0;      // std140 element stride (arrays)
			uint32_t binding = 0;     // texture unit of a sampler
			bool isSampler = false;   // a texture slot rather than a plain uniform
			bool hasRange = false;
			bool hasStep = false;
			bool isColor = false;     // force a colour picker rather than N number fields
			bool isUi = true;         // false hides engine-owned parameters from the designer
		};

		/** The render state the material block declares - what filament compiles into
		 * MAT_BLEN / MAT_CUMO / MAT_DWRIT. An absent field means the material said nothing about
		 * it, and the instance keeps the engine's default.
		 *
		 * This is the definition's state, not the instance's: a material starts from it and may
		 * override it afterwards (a .ovmat's settings, or the call site's Set* calls). */
		struct State
		{
			std::optional<bool> blendable;        // blending: opaque = false, transparent = true
			std::optional<Rendering::Settings::EBlendMode> blendMode; // opaque/transparent/add/...
			std::optional<bool> backfaceCulling;  // culling: back
			std::optional<bool> frontfaceCulling; // culling: front
			std::optional<bool> depthWriting;     // depthWrite
		};

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
		Variants m_variants;
		std::vector<Parameter> m_parameters;
		bool m_hasParameterTable = false;
		uint32_t m_parameterBlockSize = 0;
		State m_state;
		bool m_hasState = false;
	};
}
