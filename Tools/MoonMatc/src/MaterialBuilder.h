#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

/** What the material file parser accumulates: filament-matp's MaterialBuilder.
 *
 * The parser owns no state of its own - every block handler (material / vertex / geometry /
 * fragment) writes into this object, and the generator and the packager read it back. It is
 * still called MaterialFile at the call sites in Matc.cpp; `using MaterialFile = MaterialBuilder`
 * below keeps those compiling while the parser is moved out of that file in steps. */
namespace Moon::Matc
{
	/** One entry of `parameters : [ { type : float3, name : u_Albedo }, ... ]`. `count` is the
	 * array size when the parameter is one - `{ type : float3, name : samples, count : 64 }`
	 * is the SSAO kernel, which used to have to declare its uniform by hand. `default` is the
	 * GLSL initializer the old hand-written uniforms had (`uniform float x = 0.5;`): it is
	 * emitted as it is, so GL seeds the property with that value and a material whose uniforms
	 * are never set keeps the number it always had.
	 *
	 * The designer fields are optional: `label`, `group`, `min`/`max`/`step`, `color` and
	 * `ui`. A material that declares none of them gets the runtime's fallbacks (a label from
	 * the name, groups by kind, a range from the value), which is what the shaders that have
	 * not been annotated yet do. Filament's material designer reads the same kind of schema;
	 * here it lives in the material file itself, next to the parameter it describes. */
	struct Parameter
	{
		std::string type;
		std::string name;
		std::string count;
		std::string defaultValue;
		std::string label;
		std::string group;
		/** Which 1x1 texture the runtime binds for a sampler nobody assigned, `fallback : ...`
		 * in the material file. Empty means the engine's default (white for sampler2D, black for
		 * samplerCube). "flatNormal" is the neutral tangent-space normal (0, 0, 1), which a
		 * normal map samples as "no perturbation" - without it the white fallback would decode
		 * to (1, 1, 1) and bend every normal. */
		std::string fallback;
		float min = 0.0f;
		float max = 0.0f;
		float step = 0.0f;
		bool hasRange = false;
		bool hasStep = false;
		bool color = false;
		bool ui = true;

		// Where the parameter lives once the material is a descriptor: a std140 offset/stride
		// for the non-samplers (the per-material uniform block), a texture unit for the samplers.
		// Filled by LayoutParameters.
		uint32_t offset = 0;
		uint32_t stride = 0;
		uint32_t binding = 0;
	};

	/** The three stages a material can own. */
	enum class Stage
	{
		Vertex,
		Fragment,
		/** GLSL's third stage, which this engine's line/edge expansion shaders use and
		 * filament's material model has no room for. Always written by the material. */
		Geometry
	};

	inline const char* StageName(Stage p_stage)
	{
		switch (p_stage)
		{
		case Stage::Vertex: return "vertex";
		case Stage::Fragment: return "fragment";
		default: return "geometry";
		}
	}

	struct PassVariant
	{
		std::string pass;                  // empty for the default pass
		std::set<std::string> features;    // the #define set this variant is compiled with
	};

	struct MaterialBuilder
	{
		std::string name;
		std::string shadingModel = "unlit";
		/** "surface" (shaded geometry, transformed by the model matrix) or "postProcess"
		 * (a full-screen pass: the vertex stage is the quad, not the mesh). Filament splits
		 * the same two cases with MaterialBuilder::materialDomain. */
		std::string domain = "surface";
		std::map<std::string, std::string> state; // blending, culling, depthWrite, ...
		std::vector<Parameter> parameters;
		/** The std140 size of the per-material uniform block: what the runtime allocates and
		 * uploads, and what the generated GLSL's block must describe. 0 when every parameter is
		 * a sampler (or there are none). */
		uint32_t parameterBlockSize = 0;
		std::vector<std::string> features;        // declared feature bits
		std::vector<std::string> variantFilter;   // Filament-style engine variant bits (EVariantBits names)
		/** The feature set the archive build compiles with when the material enumerates engine
		 * variants (variantFilter). Empty means "no optional feature"; hasDefaultFeatures tells
		 * an explicit empty list from "not declared". */
		std::vector<std::string> defaultFeatures;
		bool hasDefaultFeatures = false;
		std::vector<std::string> vertexAttributes; // declared vertex layout, in location order
		std::vector<std::string> passes;          // extra passes, default pass implied
		/** The MaterialInputs fields this material's own fragment code writes. Filled right
		 * after parsing; decides the MATERIAL_HAS_* defines and whether the generated stages
		 * need a tangent frame. */
		std::set<std::string> writtenFields;
		/** filament's material flag `specularAntiAliasing`: compile the geometric specular AA
		 * into the material (GEOMETRIC_SPECULAR_AA) instead of branching on a uniform. */
		bool specularAntiAliasing = false;
		/** filament's `clearCoatIorChange`, on by default there too: the layer under a clear
		 * coat sees a 1.5 -> 1.5 interface instead of air -> 1.5. */
		bool clearCoatIorChange = true;
		std::string vertexBody;                   // the vertex { ... } block, verbatim
		std::string fragmentBody;                 // the fragment { ... } block, verbatim
		/** The geometry { } block, verbatim. Our own extension: GLSL has a third stage, and
		 * the four shaders that expand lines and triangle edges into screen-space geometry
		 * need it (filament's material model has no such stage). */
		std::string geometryBody;
		uint32_t vertexEntryLine = 1;             // where those blocks start, for #line
		uint32_t fragmentEntryLine = 1;
		uint32_t geometryEntryLine = 1;
	};

	/** Transitional: the rest of Matc.cpp still says MaterialFile. Removed once the parser and
	 * the generator have both moved to their own files (phase 1 of docs/MaterialPipeline.md). */
	using MaterialFile = MaterialBuilder;
}
