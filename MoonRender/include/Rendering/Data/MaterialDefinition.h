#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Rendering/Data/DescriptorSetLayout.h>
#include <Rendering/Settings/EBlendMode.h>

/** The immutable data a material is made of: what the material package described, with no GL
 * object and no value in it.
 *
 * Filament's MaterialDefinition is the parsed package (uniform interface block, sampler
 * interface block, render state, variant list); a Material holds it and a MaterialInstance
 * reads it to find where a value belongs. This is the same split, for the data this engine
 * currently needs: the PerMaterial uniform block (UboBlockInfo), its samplers (SamplerBlockInfo),
 * the descriptor-set layout, the render state and the reflected parameters.
 *
 * It is built once, from the package (or from a Shader that was built without one), and then
 * shared by every Material and MaterialInstance that uses that shader. Because it is immutable,
 * a hot reload replaces the shared pointer rather than mutating a definition instances are
 * reading. */
namespace Rendering::Data
{
	class MaterialParser;
	class Material;

	/** One parameter the material declares (the runtime form of the package's reflection). */
	struct MaterialParameter
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
		uint8_t fallback = 0;     // the 1x1 texture bound when no map is assigned (SamplerFallback)
		bool isSampler = false;   // a texture slot rather than a plain uniform
		bool hasRange = false;
		bool hasStep = false;
		bool isColor = false;     // force a colour picker rather than N number fields
		bool isUi = true;         // false hides engine-owned parameters from the designer
	};

	/** The render state the material block declares. An absent field means the material said
	 * nothing about it, and the instance keeps the engine's default. */
	struct MaterialState
	{
		std::optional<bool> blendable;
		std::optional<Rendering::Settings::EBlendMode> blendMode;
		std::optional<bool> backfaceCulling;
		std::optional<bool> frontfaceCulling;
		std::optional<bool> depthWriting;
	};

	/** One field of the PerMaterial uniform block (filament's UboBlockInfo). */
	struct MaterialUniformField
	{
		std::string name;
		std::string type;
		uint32_t offset = 0;
		uint32_t stride = 0;
		uint32_t count = 1;
	};

	/** One texture descriptor of the PerMaterial set (filament's SamplerBlockInfo). */
	struct MaterialSamplerField
	{
		std::string name;
		std::string type;
		uint32_t binding = 0;
		uint8_t fallback = 0;
	};

	class MaterialDefinition
	{
	public:
		using Parameter = MaterialParameter;
		using State = MaterialState;
		using UniformField = MaterialUniformField;
		using SamplerField = MaterialSamplerField;

		/** The parser is the definition's package reader: reflection, render state and the
		 * per-variant stage sources all come from it. */
		static std::shared_ptr<const MaterialDefinition> FromParser(
			std::shared_ptr<const MaterialParser> p_parser);

		const MaterialParser* Parser() const { return m_parser.get(); }

		/** The fallback for a material built from source (the present pass, the debug line):
		 * whatever the driver's reflection ended up in the material becomes the definition. */
		static std::shared_ptr<const MaterialDefinition> FromMaterial(const Material& p_material);

		const std::string& Name() const { return m_name; }
		const std::vector<Parameter>& Parameters() const { return m_parameters; }
		const std::vector<UniformField>& UniformFields() const { return m_uniformFields; }
		const std::vector<SamplerField>& SamplerFields() const { return m_samplerFields; }
		const DescriptorSetLayout& Layout() const { return m_layout; }
		uint32_t ParameterBlockSize() const { return m_parameterBlockSize; }
		const State& GetState() const { return m_state; }
		bool HasState() const { return m_hasState; }
		bool HasParameterTable() const { return m_hasParameterTable; }
		const std::vector<std::string>& DeclaredFeatures() const { return m_declaredFeatures; }

		/** The sampler field with that name, or nullptr. */
		const SamplerField* FindSampler(const std::string& p_name) const;
		/** The uniform field with that name, or nullptr. */
		const UniformField* FindUniform(const std::string& p_name) const;

	private:
		MaterialDefinition() = default;
		static std::shared_ptr<const MaterialDefinition> BuildFromParser(
			const MaterialParser& p_package);

		std::string m_name;
		std::shared_ptr<const MaterialParser> m_parser;
		std::vector<Parameter> m_parameters;
		std::vector<UniformField> m_uniformFields;
		std::vector<SamplerField> m_samplerFields;
		DescriptorSetLayout m_layout;
		State m_state;
		uint32_t m_parameterBlockSize = 0;
		bool m_hasState = false;
		bool m_hasParameterTable = false;
		std::vector<std::string> m_declaredFeatures;
	};
}
