#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "Matc.h"
#include "MaterialBuilder.h"

/** The stage code generation flow (filament's ShaderGenerator): one variant of one stage is
 * assembled from the material's prelude (parameter block, material inputs, varyings), the
 * variant's #define set, the skeleton main() the material does not write, the material's own
 * body, and - for the stages this engine writes itself - the geometry-stage guards.
 *
 * Split out of Matc.cpp; the prelude / layout helpers still live next to the flow in
 * ShaderGenerator.cpp and move to their own modules (UibGenerator / SibGenerator) next. */
namespace Moon::Matc
{
	/** One vertex attribute a generated stage declares: `layout (location = N) in T name;`. */
	struct VertexAttribute
	{
		uint32_t location = 0;
		std::string glslType;
		std::string name;
	};

	/** The MaterialInputs fields the material's own fragment code writes. */
	std::set<std::string> WrittenInputFields(const std::string& p_body);

	/** The MATERIAL_HAS_<define> suffixes those fields turn into, in surface-model order. */
	std::vector<std::string> WrittenInputDefines(const std::set<std::string>& p_writtenFields);

	/** Where a shared stage carries its variant defines: the package stores one copy per stage
	 * and the runtime splices the variant's defines in (filament's dictionary chunk idea). */
	inline constexpr char kDefinesPlaceholder[] = "//@MOON_VARIANT_DEFINES@";

	/** Whether those fields need a tangent frame (normal / anisotropy read it). */
	bool NeedsTangentFrame(const std::set<std::string>& p_writtenFields);

	/** The generated MaterialInputs struct and its accessor declarations. */
	std::string MaterialInputsPrelude(
		const std::string& p_shadingModel,
		const std::set<std::string>& p_writtenFields,
		const MaterialBuilder& p_material);

	/** Whether this variant needs the geometry stage for a material that expands lines. */
	bool WantsGeometryStage(const std::string& p_body, const PassVariant& p_variant);

	/** The attributes a stage text declares, read back out of the generated `layout (...)` lines. */
	std::vector<VertexAttribute> CollectVertexAttributes(const std::string& p_stageText);

	/** True for the sampler types of the material DSL (sampler2D / samplerCube / ...). */
	bool IsSamplerType(const std::string& p_dslType);

	/** Which 1x1 texture the runtime binds for an unassigned sampler (0 = default, 1 = flat normal). */
	uint8_t SamplerFallbackKind(const std::string& p_name);

	/** The GLSL spelling of a material DSL type. */
	std::string GlslType(const std::string& p_type);

	/** Fills each parameter's std140 offset / stride and the block size (LayoutParameters). */
	void LayoutParameters(MaterialBuilder& p_material);

	/** Adds the engine-owned globals (camera, lights, shadow matrix) to the block. */
	void InjectEngineGlobals(MaterialBuilder& p_material);

	/** The variant's #define set: the engine bits it carries plus the stage's feature defines. */
	std::string VariantDefines(const PassVariant& p_variant, Stage p_stage, const std::string& p_stageText = {});

	/** The whole stage: prelude + defines + body + main. Empty with p_error set on failure. */
	class ShaderGenerator
	{
	public:
		static std::string Build(
			Stage p_stage,
			const MaterialBuilder& p_material,
			const PassVariant& p_variant,
			const std::string& p_prelude,
			const std::string& p_fileName,
			const std::vector<std::string>& p_includeDirs,
			const IncludeResolver& p_includeResolver,
			std::string& p_error,
			bool p_shared = false);
	};
}
