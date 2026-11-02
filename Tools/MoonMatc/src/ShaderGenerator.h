#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "Matc.h"
#include "MaterialBuilder.h"

/** The stage code generator (filament's filamat/src/shaders/ShaderGenerator).
 *
 * One instance is the generator for one material: it holds the material, the MaterialInputs
 * fields the material's fragment code writes, the common prelude and the include resolver.
 * Everything that decides what a stage looks like - defines, varyings, attributes, skeleton
 * main, MaterialInputs struct, lit-surface stack, #line placement - is a member method here;
 * CompileSource only asks for stages and packages the result.
 */
namespace Moon::Matc
{
	/** One vertex attribute a generated stage declares: `layout (location = N) in T name;`. */
	struct VertexAttribute
	{
		uint32_t location = 0;
		std::string glslType;
		std::string name;
	};

	/** Where a shared stage carries its variant defines: the package stores one copy per stage
	 * and the runtime splices the variant's defines in (filament's dictionary chunk idea). */
	inline constexpr char kDefinesPlaceholder[] = "//@MOON_VARIANT_DEFINES@";

	class ShaderGenerator
	{
	public:
		ShaderGenerator(
			const MaterialBuilder& p_material,
			std::set<std::string> p_writtenFields,
			std::string p_fileName,
			std::vector<std::string> p_includeDirs,
			IncludeResolver p_includeResolver);

		/** The MaterialInputs fields the material's own fragment code writes. Static because it
		 * is a property of the source text, before a generator exists. */
		static std::set<std::string> WrittenInputFields(const std::string& p_body);

		/** The MATERIAL_HAS_<define> suffixes those fields turn into, in surface-model order. */
		static std::vector<std::string> WrittenInputDefines(const std::set<std::string>& p_writtenFields);

		/** Whether those fields need a tangent frame (normal / anisotropy read it). */
		static bool NeedsTangentFrame(const std::set<std::string>& p_writtenFields);

		/** The attributes a generated stage declares, read back out of its `layout (...)` lines. */
		static std::vector<VertexAttribute> CollectVertexAttributes(const std::string& p_stageText);

		/** The whole stage: version, defines, prelude, entry point, generated main. Empty with
		 * Error() set on failure. With p_shared the variant defines become the placeholder. */
		std::string Build(Stage p_stage, const PassVariant& p_variant, bool p_shared = false);

		/** The #define lines for one variant of a stage (the package stores them per entry). */
		std::string VariantDefines(
			const PassVariant& p_variant,
			Stage p_stage,
			const std::string& p_stageText = {}) const;

		/** Whether this variant needs the material's geometry block (the guard-feature rule). */
		bool WantsGeometryStage(const PassVariant& p_variant) const;

		const std::string& Error() const { return m_error; }

	private:
		/** EngineUBO + Utils + MaterialInputs: the part every stage shares. */
		std::string BuildPrelude() const;
		std::string MaterialInputsPrelude() const;
		std::string VaryingsPrelude(bool p_vertex, bool p_needsTbn) const;
		std::string AttributeDeclarations(Stage p_stage) const;
		std::string VertexSkeleton(bool p_hasUserVertex, bool p_needsTbn) const;
		std::string FragmentSkeleton() const;
		std::string LitSurfacePrelude(bool p_skeletonStage) const;

		static bool DefinesMain(const std::string& p_body);
		static int GeometryGuardFeature(const std::string& p_body, std::string& p_feature);
		static bool GuardWrapsBlock(const std::string& p_body);

		const MaterialBuilder& m_material;
		std::set<std::string> m_writtenFields;
		std::string m_fileName;
		std::vector<std::string> m_includeDirs;
		IncludeResolver m_includeResolver;
		std::string m_prelude;
		std::string m_error;
	};
}
