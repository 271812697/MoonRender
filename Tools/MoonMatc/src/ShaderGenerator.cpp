// The stage generator: see ShaderGenerator.h. Split out of Matc.cpp - the whole prelude /
// skeleton / variant-define / stage-assembly block now lives here.

#include "ShaderGenerator.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

#include "FileUtils.h"
#include "Includes.h"
#include "SibGenerator.h"
#include "TextUtils.h"
#include "UibGenerator.h"

namespace Moon::Matc
{
	// ---------------------------------------------------------------------------------
	// Code generation: prelude + entry points + main
	// ---------------------------------------------------------------------------------

	/** When a MaterialInputs field exists, and when initMaterial() gives it a default. Two
	 * separate guards because filament's own header does that: `clearCoat` and `anisotropy`
	 * are always declared but only initialized when the material uses them. */
	enum class InputGuard
	{
		Always,
		Lit,                 // !defined(SHADING_MODEL_UNLIT)
		Has,                 // defined(MATERIAL_HAS_<this field>)
		HasNormal,           // defined(MATERIAL_HAS_NORMAL)
		HasBentNormal,       // defined(MATERIAL_HAS_BENT_NORMAL)
		HasClearCoatNormal,  // defined(MATERIAL_HAS_CLEAR_COAT) && defined(MATERIAL_HAS_CLEAR_COAT_NORMAL)
		HasPostLightingColor,// defined(MATERIAL_HAS_POST_LIGHTING_COLOR)
	};

	struct InputField
	{
		const char* name;
		const char* define;      // the suffix: MATERIAL_HAS_<define>
		const char* type;
		const char* initializer;
		InputGuard declaration;
		InputGuard initialization;
	};

	/** filament's shaders/src/surface_material_inputs.fs, field for field and guard for guard,
	 * minus the shading models this engine does not have (cloth / subsurface / specular
	 * glossiness). Keeping the order and the defaults identical is what makes the generated
	 * struct comparable with the reference file - the surface prelude reads these fields. */
	constexpr InputField kInputFields[] = {
		{ "baseColor",            "BASE_COLOR",            "vec4",  "vec4(1.0)",                  InputGuard::Always, InputGuard::Always },
		{ "roughness",            "ROUGHNESS",             "float", "1.0",                        InputGuard::Lit,    InputGuard::Lit },
		{ "metallic",             "METALLIC",              "float", "0.0",                        InputGuard::Lit,    InputGuard::Lit },
		{ "reflectance",          "REFLECTANCE",           "float", "0.5",                        InputGuard::Lit,    InputGuard::Lit },
		{ "ambientOcclusion",     "AMBIENT_OCCLUSION",     "float", "1.0",                        InputGuard::Lit,    InputGuard::Lit },
		{ "emissive",             "EMISSIVE",              "vec4",  "vec4(vec3(0.0), 1.0)",       InputGuard::Always, InputGuard::Always },
		{ "sheenColor",           "SHEEN_COLOR",           "vec3",  "vec3(0.0)",                  InputGuard::Has,    InputGuard::Has },
		{ "sheenRoughness",       "SHEEN_ROUGHNESS",       "float", "0.0",                        InputGuard::Has,    InputGuard::Has },
		{ "clearCoat",            "CLEAR_COAT",            "float", "1.0",                        InputGuard::Always, InputGuard::Has },
		{ "clearCoatRoughness",   "CLEAR_COAT_ROUGHNESS",  "float", "0.0",                        InputGuard::Always, InputGuard::Has },
		{ "anisotropy",           "ANISOTROPY",            "float", "0.0",                        InputGuard::Always, InputGuard::Has },
		{ "anisotropyDirection",  "ANISOTROPY_DIRECTION",  "vec3",  "vec3(1.0, 0.0, 0.0)",        InputGuard::Always, InputGuard::Has },
		{ "thickness",            "THICKNESS",             "float", "0.5",                        InputGuard::Lit,    InputGuard::Lit },
		{ "subsurfacePower",      "SUBSURFACE_POWER",      "float", "12.234",                     InputGuard::Has,    InputGuard::Has },
		{ "subsurfaceColor",      "SUBSURFACE_COLOR",      "vec3",  "vec3(1.0)",                  InputGuard::Has,    InputGuard::Has },
		{ "absorption",           "ABSORPTION",            "vec3",  "vec3(0.0)",                  InputGuard::Lit,    InputGuard::Has },
		{ "transmission",         "TRANSMISSION",          "float", "1.0",                        InputGuard::Lit,    InputGuard::Has },
		{ "ior",                  "IOR",                   "float", "1.5",                        InputGuard::Lit,    InputGuard::Has },
		{ "specularFactor",       "SPECULAR_FACTOR",       "float", "1.0",                        InputGuard::Has,    InputGuard::Has },
		{ "specularColorFactor",  "SPECULAR_COLOR_FACTOR", "vec3",  "vec3(1.0)",                  InputGuard::Has,    InputGuard::Has },
		{ "shadowStrength",       "SHADOW_STRENGTH",       "float", "0.0",                        InputGuard::Has,    InputGuard::Has },
		{ "normal",               "NORMAL",                "vec3",  "vec3(0.0, 0.0, 1.0)",        InputGuard::HasNormal, InputGuard::HasNormal },
		{ "bentNormal",           "BENT_NORMAL",           "vec3",  "vec3(0.0, 0.0, 1.0)",        InputGuard::HasBentNormal, InputGuard::HasBentNormal },
		{ "clearCoatNormal",      "CLEAR_COAT_NORMAL",     "vec3",  "vec3(0.0, 0.0, 1.0)",        InputGuard::HasClearCoatNormal, InputGuard::HasClearCoatNormal },
		{ "postLightingColor",    "POST_LIGHTING_COLOR",   "vec4",  "vec4(0.0)",                  InputGuard::HasPostLightingColor, InputGuard::HasPostLightingColor },
		{ "postLightingMixFactor","POST_LIGHTING_MIX_FACTOR","float","1.0",                        InputGuard::HasPostLightingColor, InputGuard::HasPostLightingColor },
	};

	ShaderGenerator::ShaderGenerator(
		const MaterialBuilder& p_material,
		std::set<std::string> p_writtenFields,
		std::string p_fileName,
		std::vector<std::string> p_includeDirs,
		IncludeResolver p_includeResolver)
		: m_material(p_material)
		, m_writtenFields(std::move(p_writtenFields))
		, m_fileName(std::move(p_fileName))
		, m_includeDirs(std::move(p_includeDirs))
		, m_includeResolver(std::move(p_includeResolver))
	{
		m_prelude = BuildPrelude();
	}

	std::string ShaderGenerator::BuildPrelude() const
	{
		// The two headers every stage gets, then the MaterialInputs struct and its defines.
		std::string prelude;
		for (const std::string& relative : { "Common/Buffers/EngineUBO.glsl", "Common/Utils.glsl" })
		{
			std::string content;
			bool found = false;
			if (m_includeResolver)
			{
				std::string origin;
				found = m_includeResolver(relative, content, origin);
			}
			if (!found)
			{
				for (const std::string& dir : m_includeDirs)
				{
					content = ReadFile((std::filesystem::path(dir) / relative).string(), found);
					if (found)
					{
						break;
					}
				}
			}
			if (found)
			{
				prelude += "\n#line 1 \"" + relative + "\"\n" + content + "\n";
			}
		}

		prelude += "\n#line 1 \"<material inputs>\"\n" + MaterialInputsPrelude();
		return prelude;
	}

	/** The fields a material's own code writes. filament runs a real AST pass for this
	 * (libs/filamat/src/sca/ASTHelpers.cpp, PropertyFinder); anchoring the scan on the
	 * variables that are declared as `MaterialInputs` gets the same answer for code that
	 * names them, and cannot mistake another struct's fields (`FilamentMaterialInputs
	 * material;` in the Filament port) for these. The scan only sees the material's own
	 * block, not what an #include writes - those are the engine's, and they write their own
	 * struct in the current code. */
	std::set<std::string> ShaderGenerator::WrittenInputFields(const std::string& p_body)
	{
		std::set<std::string> written;

		static const std::regex kDeclaration(R"(\bMaterialInputs\s+([A-Za-z_][A-Za-z0-9_]*))");
		std::vector<std::string> variables;
		for (auto it = std::sregex_iterator(p_body.begin(), p_body.end(), kDeclaration);
			it != std::sregex_iterator(); ++it)
		{
			const std::string name = (*it)[1].str();
			if (std::ranges::find(variables, name) == variables.end())
			{
				variables.push_back(name);
			}
		}

		for (const std::string& variable : variables)
		{
			// `m.roughness = ...`, `m.baseColor.rgb = ...`, `m.emissive += ...`: any component
			// and any compound assignment counts as "the material writes this field". A
			// comparison (`==`) is a read.
			const std::regex write("\\b" + variable
				+ R"(\s*\.\s*([A-Za-z_][A-Za-z0-9_]*)(\s*\.\s*[A-Za-z_][A-Za-z0-9_]*)*\s*(\+|-|\*|/)?=(?!=))");
			for (auto it = std::sregex_iterator(p_body.begin(), p_body.end(), write);
				it != std::sregex_iterator(); ++it)
			{
				written.insert((*it)[1].str());
			}
		}
		return written;
	}

	/** filament's synthetic MATERIAL_NEEDS_TBN: the four fields that read the tangent frame. */
	bool ShaderGenerator::NeedsTangentFrame(const std::set<std::string>& p_writtenFields)
	{
		return p_writtenFields.contains("normal")
			|| p_writtenFields.contains("bentNormal")
			|| p_writtenFields.contains("clearCoatNormal")
			|| p_writtenFields.contains("anisotropy");
	}

	/** The MaterialInputs the material() entry point writes, the function that fills them
	 * with defaults, and the function that turns them into a colour.
	 *
	 * Names and shape follow filament: `MaterialInputs inputs; initMaterial(inputs);
	 * material(inputs); fragColor = evaluateMaterial(inputs);` is exactly what its generated
	 * surface_main.fs does (shaders/src/surface_main.fs:50-56); the fields, their order, their
	 * defaults and their guards are surface_material_inputs.fs. The `MATERIAL_HAS_*` defines at
	 * the top are the material-level half of filament's material system: they say which of
	 * these fields this material actually writes, so the surface prelude can compile the
	 * unused lobes out. */
	std::string ShaderGenerator::MaterialInputsPrelude() const
	{
		const bool lit = (m_material.shadingModel != "unlit");
		std::ostringstream out;

		const auto inputGuardOpen = [](InputGuard p_guard, const InputField& p_field) -> std::string
		{
			switch (p_guard)
			{
			case InputGuard::Always: return {};
			case InputGuard::Lit: return "#if !defined(SHADING_MODEL_UNLIT)\n";
			case InputGuard::Has: return std::string("#if defined(MATERIAL_HAS_") + p_field.define + ")\n";
			case InputGuard::HasNormal: return "#if defined(MATERIAL_HAS_NORMAL)\n";
			case InputGuard::HasBentNormal: return "#if defined(MATERIAL_HAS_BENT_NORMAL)\n";
			case InputGuard::HasClearCoatNormal:
				return "#if defined(MATERIAL_HAS_CLEAR_COAT) && defined(MATERIAL_HAS_CLEAR_COAT_NORMAL)\n";
			case InputGuard::HasPostLightingColor: return "#if defined(MATERIAL_HAS_POST_LIGHTING_COLOR)\n";
			}
			return {};
		};

		// --- the material's own defines -------------------------------------------------
		out << "// material defines (filament's MATERIAL_HAS_*): derived from what the code below\n"
			<< "// writes, so the surface prelude compiles the unused lobes out.\n"
			<< (lit ? "#define SHADING_MODEL_LIT\n" : "#define SHADING_MODEL_UNLIT\n");
		for (const InputField& field : kInputFields)
		{
			if (m_writtenFields.contains(field.name))
			{
				out << "#define MATERIAL_HAS_" << field.define << "\n";
			}
		}

		// filament's synthetic define: the four fields that need a tangent frame.
		const bool needsTbn = NeedsTangentFrame(m_writtenFields);
		out << "#define MATERIAL_NEEDS_TBN " << (needsTbn ? 1 : 0) << "\n"
			<< "#define GEOMETRIC_SPECULAR_AA " << (m_material.specularAntiAliasing && lit ? 1 : 0) << "\n"
			<< "#define CLEAR_COAT_IOR_CHANGE " << (m_material.clearCoatIorChange ? 1 : 0) << "\n\n";

		// --- the struct ------------------------------------------------------------------
		out << "struct MaterialInputs {\n";
		for (const InputField& field : kInputFields)
		{
			const std::string guard = inputGuardOpen(field.declaration, field);
			if (!guard.empty())
			{
				out << "  " << guard;
			}
			out << "    " << field.type << " " << field.name << ";\n";
			if (!guard.empty())
			{
				out << "#endif\n";
			}
		}
		out << "};\n\n";

		// --- the defaults ----------------------------------------------------------------
		out << "void initMaterial(out MaterialInputs m) {\n";
		for (const InputField& field : kInputFields)
		{
			const std::string guard = inputGuardOpen(field.initialization, field);
			if (!guard.empty())
			{
				out << "  " << guard;
			}
			out << "    m." << field.name << " = " << field.initializer << ";\n";
			if (!guard.empty())
			{
				out << "#endif\n";
			}
		}
		out << "}\n\n"
			// The .shader files call it prepareMaterial(); keeping the alias means a shader
			// written either way compiles.
			<< "void prepareMaterial(out MaterialInputs m) { initMaterial(m); }\n\n";

		// --- the shading model's verdict --------------------------------------------------
		// Unlit is the whole of what this engine can evaluate so far; a lit material's
		// evaluateMaterial() comes from the surface prelude that matc splices in for the
		// `lit` shading model, so it is only forward-declared here.
		out << (lit
			? "vec4 evaluateMaterial(inout MaterialInputs m);\n"
			// filament's surface_shading_unlit.fs: the base colour plus the emissive's rgb;
			// the emissive does not touch alpha.
			: "vec4 evaluateMaterial(inout MaterialInputs m) {\n"
			  "    vec4 color = m.baseColor;\n"
			  "#if defined(MATERIAL_HAS_EMISSIVE)\n"
			  "    color.rgb += m.emissive.rgb;\n"
			  "#endif\n"
			  "    return color;\n"
			  "}\n");
		return out.str();
	}

	/** The varyings both stages agree on. Part of the prelude - the material's entry point
	 * may read them, so they have to be declared before it, which is what filament does in
	 * surface_varyings.glsl (and what post_process_main.vs/.fs pass instead of them).
	 *
	 * `p_needsTbn` adds the world tangent frame, filament's `shading_tangentToWorld`: a material
	 * that writes `material.normal` / `material.anisotropyDirection` reads it, and a material
	 * that writes neither never pays for it. */
	std::string ShaderGenerator::VaryingsPrelude(bool p_vertex, bool p_needsTbn) const
	{
		std::ostringstream out;
		if (m_material.domain == "postProcess")
		{
			// A full-screen pass only carries texture coordinates between the stages.
			out << (p_vertex ? "out vec2 TexCoords;\n\n" : "in vec2 TexCoords;\n\n");
			return out.str();
		}
		out << (p_vertex ? "out VS_OUT {\n" : "in VS_OUT {\n")
			<< "    vec3 FragPos;\n"
			<< "    vec2 TexCoords;\n"
			<< "    vec3 Normal;\n";
		if (p_needsTbn)
		{
			out << "#if MATERIAL_NEEDS_TBN\n"
				<< "    mat3 TBN; // world tangent frame: T, B, N (filament's shading_tangentToWorld)\n"
				<< "#endif\n";
		}
		out << (p_vertex ? "} vs_out;\n\n" : "} fs_in;\n\n");
		return out.str();
	}

	/** The feature a geometry body's leading guard is about, if it is a plain single-feature
	 * test: `#ifdef F` / `#if defined(F)` / `#if F` are +1, the negations are -1, and anything
	 * fancier (`||`, `&&`, comparisons) is 0 "do not try to be clever". */
	int ShaderGenerator::GeometryGuardFeature(const std::string& p_body, std::string& p_feature)
	{
		std::istringstream stream(p_body);
		std::string line;
		while (std::getline(stream, line))
		{
			const std::string trimmed = Trim(line);
			if (trimmed.empty())
			{
				continue;
			}
			if (!trimmed.starts_with("#if"))
			{
				return 0;
			}

			bool negated = false;
			std::string condition;
			if (trimmed.starts_with("#ifdef ") || trimmed.starts_with("#ifndef "))
			{
				negated = trimmed.starts_with("#ifndef ");
				condition = Trim(trimmed.substr(7));
			}
			else if (trimmed.starts_with("#if "))
			{
				condition = Trim(trimmed.substr(4));
				if (condition.starts_with("!"))
				{
					negated = true;
					condition = Trim(condition.substr(1));
				}
				if (condition.starts_with("defined"))
				{
					condition = Trim(condition.substr(7));
					if (!condition.empty() && condition.front() == '(')
					{
						condition = Trim(condition.substr(1));
					}
				}
			}
			else
			{
				return 0;
			}

			// A guard this helper understands is exactly one identifier, nothing after it.
			std::string name;
			for (const char c : condition)
			{
				if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') { name += c; }
				else { break; }
			}
			if (name.empty() || Trim(condition.substr(name.size())) != ")" && !Trim(condition.substr(name.size())).empty())
			{
				return 0;
			}
			if (name.starts_with("defined") || name.starts_with("GL_") || name.starts_with("MOON_"))
			{
				// Not one of the material's own features.
				return 0;
			}

			p_feature = name;
			return negated ? -1 : 1;
		}
		return 0;
	}

	/** True when the block's first `#if` is closed by an `#endif` with nothing but whitespace
	 * after it - i.e. the guard covers the whole stage body rather than one helper in it. */
	bool ShaderGenerator::GuardWrapsBlock(const std::string& p_body)
	{
		std::istringstream stream(p_body);
		std::string line;
		int depth = 0;
		bool opened = false;
		std::string rest;
		while (std::getline(stream, line))
		{
			const std::string trimmed = Trim(line);
			if (!opened)
			{
				if (trimmed.starts_with("#if")) { opened = true; depth = 1; }
				else if (!trimmed.empty()) { return false; }
				continue;
			}
			if (trimmed.starts_with("#if")) { ++depth; }
			else if (trimmed.starts_with("#endif"))
			{
				if (--depth == 0)
				{
					// Everything after the closing #endif must be whitespace.
					std::string tail;
					std::getline(stream, tail, '\0');
					return Trim(tail).empty();
				}
			}
		}
		return false;
	}

	/** Whether this variant carries the geometry stage.
	 *
	 * A material declares one geometry block, but the shaders that use one open it with the
	 * feature that needs it (`#if defined(WITH_EDGE)`), and the old pipeline skipped the whole
	 * stage for the variants where that feature was off (ShaderLoader's stage-skip hack). The
	 * same rule here: a variant that would only run a pass-through geometry shader does not
	 * get one - no blob, nothing to compile, nothing to run per draw.
	 *
	 * Only a guard that wraps the WHOLE block counts (first line `#if`, matching `#endif` at the
	 * end of the block): a guard around part of it says nothing about whether the stage is
	 * needed, and those keep their geometry stage. */
	bool ShaderGenerator::WantsGeometryStage(const PassVariant& p_variant) const
	{
		std::string feature;
		const int sense = GeometryGuardFeature(m_material.geometryBody, feature);
		if (sense == 0 || !GuardWrapsBlock(m_material.geometryBody))
		{
			return true;
		}
		const bool present = p_variant.features.contains(feature);
		return sense > 0 ? present : !present;
	}

	/** True when a block defines its own `void main()`. That is the escape hatch for the few
	 * shaders whose stage is not "adjust the attributes, then the fixed transform" - a
	 * billboard that rebuilds the model-view matrix, a skybox that writes w = z. Filament
	 * keeps the transform out of the material too, so a material that owns its stage is our
	 * deviation, documented in docs/ShaderPipeline.md (section 10, differences from Filament). */
	bool ShaderGenerator::DefinesMain(const std::string& p_body)
	{
		size_t at = 0;
		while ((at = p_body.find("void", at)) != std::string::npos)
		{
			size_t i = at + 4;
			while (i < p_body.size() && std::isspace(static_cast<unsigned char>(p_body[i]))) { ++i; }
			if (p_body.compare(i, 4, "main") == 0)
			{
				i += 4;
				while (i < p_body.size() && std::isspace(static_cast<unsigned char>(p_body[i]))) { ++i; }
				if (i < p_body.size() && p_body[i] == '(')
				{
					return true;
				}
			}
			at += 4;
		}
		return false;
	}

	/** The vertex main: the transform the entry point is not allowed to touch. Copied from
	 * what Unlit/Standard.shader do by hand today, and from filament's post_process_main.vs for
	 * the full-screen case.
	 *
	 * Only generated for a stage that does not own its main() - see BuildStage. */
	std::string ShaderGenerator::VertexSkeleton(bool p_hasUserVertex, bool p_needsTbn) const
	{
		std::ostringstream out;
		if (m_material.domain == "postProcess")
		{
			// The quad comes in in clip space; there is no model matrix and no normal.
			out << "layout (location = 0) in vec2 geo_Pos;\n"
				<< "layout (location = 1) in vec2 geo_TexCoords;\n\n"
				<< "void main() {\n"
					<< "    TexCoords = geo_TexCoords;\n"
					<< "    gl_Position = vec4(geo_Pos, 0.0, 1.0);\n"
					<< "}\n";
			return out.str();
		}
		out << "layout (location = 0) in vec3 geo_Pos;\n"
			<< "layout (location = 1) in vec2 geo_TexCoords;\n"
			<< "layout (location = 2) in vec3 geo_Normal;\n";
		if (p_needsTbn)
		{
			// Geometry::Vertex: position, texCoords, normal, tangent, bitangent.
			out << "#if MATERIAL_NEEDS_TBN\n"
				<< "layout (location = 3) in vec3 geo_Tangent;\n"
				<< "layout (location = 4) in vec3 geo_Bitangent;\n"
				<< "#endif\n";
		}
		// The stub is only for a material that has no vertex block of its own; a material
		// that has one would otherwise get two definitions of the same function.
		if (!p_hasUserVertex)
		{
			out << "void materialVertex(inout vec3 position, inout vec2 texCoords, inout vec3 normal) { }\n\n";
		}
		out << "void main() {\n"
			<< "    vec3 position = geo_Pos;\n"
			<< "    vec2 texCoords = geo_TexCoords;\n"
			<< "    vec3 normal = geo_Normal;\n"
			<< "    materialVertex(position, texCoords, normal);\n"
			<< "    vs_out.FragPos = vec3(ubo_Model * vec4(position, 1.0));\n"
			<< "    vs_out.TexCoords = texCoords;\n"
			// The normal matrix, not the model matrix: a non-uniform scale shears the normal off
			// the surface otherwise. Every hand-written vertex stage in the engine already does
			// this (filament keeps a per-object normal matrix in ObjectUniforms instead).
			<< "    vs_out.Normal = normalize(mat3(transpose(inverse(ubo_Model))) * normal);\n";
		if (p_needsTbn)
		{
			out << "#if MATERIAL_NEEDS_TBN\n"
				<< "    vs_out.TBN = ConstructTBN(ubo_Model, normal, geo_Tangent, geo_Bitangent);\n"
				<< "#endif\n";
		}
		out << "    gl_Position = ubo_Projection * ubo_View * vec4(vs_out.FragPos, 1.0);\n"
			<< "}\n";
		return out.str();
	}

	/** The fragment main, copied in shape from filament's surface_main.fs:50-56: fill the
	 * inputs with defaults, run the material's entry point, then let the shading model turn
	 * them into a colour. Only generated for a stage that does not own its main(). */
	std::string ShaderGenerator::FragmentSkeleton() const
	{
		std::ostringstream out;
		out << "out vec4 FRAGMENT_COLOR;\n\n"
			<< "void main() {\n"
			<< "    MaterialInputs inputs;\n"
			<< "    initMaterial(inputs);\n\n"
			<< "    // Invoke user code\n"
			<< "    material(inputs);\n\n"
			<< "    FRAGMENT_COLOR = evaluateMaterial(inputs);\n"
			<< "}\n";
		return out.str();
	}

	/** The `lit` shading model's half of the generated fragment stage: the surface stack plus
	 * the two entry points into it.
	 *
	 * filament's ShaderGenerator splices the same files in for Shading::LIT
	 * (libs/filamat/src/shaders/ShaderGenerator.cpp:1151-1197): surface_lighting,
	 * surface_shadowing, surface_brdf, surface_shading_model_standard, ambient occlusion,
	 * surface_light_indirect, surface_light_directional/punctual and surface_shading_lit. Here
	 * they are the ported Lighting/Filament/* files, #included rather than embedded so there is
	 * exactly one copy of the reference code.
	 *
	 * Two entry points, because a material can be either kind of fragment stage:
	 *  - `evaluateSurface(m, fragPos, geometricNormal, shadingNormal, clearCoatNormal, v)` is
	 *    varying-agnostic and what a **raw** stage (one that writes its own `main()`, like the
	 *    CAD one with its geometry-stage edge equations) calls after it filled the inputs;
	 *  - `evaluateMaterial(m)` is the skeleton's version: it derives position, normals and the
	 *    view vector from the generated varyings and forwards to `evaluateSurface`.
	 *
	 * `MaterialInputs` -> `FilamentMaterialInputs` is the debt this slice leaves: the surface
	 * stack still takes its own struct, so the copy happens here. It goes away when the ported
	 * files take `MaterialInputs` directly. */
	std::string ShaderGenerator::LitSurfacePrelude(bool p_skeletonStage) const
	{
		std::ostringstream out;
		out << "\n#line 1 \"<lit surface>\"\n"
			<< "// The surface prelude is emitted after the material's parameters, so it can read\n"
			<< "// them plus the injected engine globals.\n"
			<< "#include \":Shaders/Lighting/Filament/surface_lighting.glsl\"\n\n"
			// filament's getWorldNormalVector(): the material writes a tangent-space normal and
			// the framework puts it in world space through the interpolated tangent frame. A
			// mesh without usable tangents (nothing generates them for the imported models yet)
			// would turn that into garbage, so a degenerate frame falls back to the geometric
			// normal instead of shading with NaNs.
			<< "vec3 tangentSpaceToWorld(mat3 tbn, vec3 tangentNormal, vec3 geometricNormal)\n"
			<< "{\n"
			<< "    const bool hasFrame = length(tbn[0]) > 1.0e-5 && length(tbn[1]) > 1.0e-5;\n"
			<< "    return hasFrame ? normalize(tbn * tangentNormal) : geometricNormal;\n"
			<< "}\n\n"
			<< "vec4 evaluateSurface(MaterialInputs m, vec3 fragPos, vec3 geometricNormal,\n"
			<< "    vec3 shadingNormal, vec3 clearCoatNormal, vec3 v)\n"
			<< "{\n"
			<< "    FilamentMaterialInputs inputs;\n"
			<< "    inputs.baseColor = m.baseColor.rgb;\n"
			<< "#if !defined(SHADING_MODEL_UNLIT)\n"
			<< "    inputs.roughness = m.roughness;\n"
			<< "    inputs.metallic = saturate(m.metallic);\n"
			<< "    inputs.reflectance = m.reflectance;\n"
			<< "    inputs.ambientOcclusion = saturate(m.ambientOcclusion);\n"
			<< "#endif\n"
			<< "#if defined(MATERIAL_HAS_CLEAR_COAT)\n"
			<< "    inputs.clearCoat = m.clearCoat;\n"
			<< "    inputs.clearCoatRoughness = m.clearCoatRoughness;\n"
			<< "#else\n"
			<< "    inputs.clearCoat = 0.0;\n"
			<< "    inputs.clearCoatRoughness = 0.0;\n"
			<< "#endif\n"
			<< "#if defined(MATERIAL_HAS_THICKNESS)\n"
			<< "    inputs.thickness = m.thickness;\n"
			<< "#else\n"
			<< "    inputs.thickness = 0.0;\n"
			<< "#endif\n"
			<< "#if defined(MATERIAL_HAS_TRANSMISSION)\n"
			<< "    inputs.transmission = m.transmission;\n"
			<< "#else\n"
			<< "    inputs.transmission = 0.0;\n"
			<< "#endif\n"
			<< "#if defined(MATERIAL_HAS_ABSORPTION)\n"
			<< "    inputs.absorption = m.absorption;\n"
			<< "#else\n"
			<< "    inputs.absorption = vec3(0.0);\n"
			<< "#endif\n"
			<< "#if defined(MATERIAL_HAS_IOR)\n"
			<< "    inputs.refractionIndex = m.ior;\n"
			<< "#else\n"
			<< "    inputs.refractionIndex = 1.5;\n"
			<< "#endif\n"
			<< "#if defined(MATERIAL_HAS_SPECULAR_FACTOR)\n"
			<< "    inputs.specularFactor = m.specularFactor;\n"
			<< "#else\n"
			<< "    inputs.specularFactor = 1.0;\n"
			<< "#endif\n"
			<< "#if defined(MATERIAL_HAS_SPECULAR_COLOR_FACTOR)\n"
			<< "    inputs.specularColorFactor = m.specularColorFactor;\n"
			<< "#else\n"
			<< "    inputs.specularColorFactor = vec3(1.0);\n"
			<< "#endif\n"
			<< "#if defined(MATERIAL_HAS_EMISSIVE)\n"
			<< "    inputs.emissive = m.emissive.rgb;\n"
			<< "#else\n"
			<< "    inputs.emissive = vec3(0.0);\n"
			<< "#endif\n\n"
			<< "    FilamentMaterialInputs filtered = inputs;\n"
			<< "#if GEOMETRIC_SPECULAR_AA\n"
			<< "    filtered.roughness = normalFiltering(inputs.roughness, geometricNormal);\n"
			<< "    filtered.clearCoatRoughness = normalFiltering(inputs.clearCoatRoughness, geometricNormal);\n"
			<< "#endif\n\n"
			<< "    const PixelParams pixel = preparePixelParams(filtered, shadingNormal, v, _BRDFLut, clearCoatNormal);\n"
			<< "    vec3 color = evaluateFilamentSurface(inputs, pixel, fragPos, shadingNormal, v,\n"
			<< "        _IrradianceCube, _PrefilterCube, _ShadowMap, _LightSpaceMatrix, _BRDFLut);\n"
			<< "    color += inputs.emissive;\n"
			<< "    return vec4(color, m.baseColor.a);\n"
			<< "}\n";

		if (p_skeletonStage)
		{
			out << "\n"
				<< "vec4 evaluateMaterial(inout MaterialInputs m)\n"
				<< "{\n"
				<< "    const vec3 v = normalize(ubo_ViewPos - fs_in.FragPos);\n"
				<< "    const vec3 geometricNormal = normalize(fs_in.Normal);\n"
				<< "#if defined(MATERIAL_HAS_NORMAL)\n"
				<< "#if MATERIAL_NEEDS_TBN\n"
				<< "    const vec3 n = tangentSpaceToWorld(fs_in.TBN, m.normal, geometricNormal);\n"
				<< "#else\n"
				<< "    const vec3 n = geometricNormal;\n"
				<< "#endif\n"
				<< "#else\n"
				<< "    const vec3 n = geometricNormal;\n"
				<< "#endif\n"
				<< "#if defined(MATERIAL_HAS_CLEAR_COAT_NORMAL)\n"
				<< "#if MATERIAL_NEEDS_TBN\n"
				<< "    const vec3 clearCoatNormal = tangentSpaceToWorld(fs_in.TBN, m.clearCoatNormal, geometricNormal);\n"
				<< "#else\n"
				<< "    const vec3 clearCoatNormal = geometricNormal;\n"
				<< "#endif\n"
				<< "#else\n"
				<< "    const vec3 clearCoatNormal = geometricNormal;\n"
				<< "#endif\n"
				<< "    return evaluateSurface(m, fs_in.FragPos, geometricNormal, n, clearCoatNormal, v);\n"
				<< "}\n";
		}
		return out.str();
	}

	/** One vertex attribute a generated stage declares: `layout (location = N) in T name;`.
	 * Read back out of the stage text rather than tracked while emitting, so a raw stage that
	 * declares its own inputs (GizmoLine's aPositionSize/aColor, the instanced ones) shows up
	 * exactly like the generated skeleton does. filament records the same pairs in its
	 * MaterialAttributeInfo chunk. */
	std::vector<VertexAttribute> ShaderGenerator::CollectVertexAttributes(const std::string& p_stageText)
	{
		static const std::regex kAttribute(
			R"(layout\s*\(\s*location\s*=\s*([0-9]+)\s*\)\s*in\s+([A-Za-z0-9_]+)\s+([A-Za-z0-9_]+))");

		std::vector<VertexAttribute> attributes;
		for (std::sregex_iterator it(p_stageText.begin(), p_stageText.end(), kAttribute), end; it != end; ++it)
		{
			VertexAttribute attribute;
			attribute.location = static_cast<uint32_t>(std::stoul((*it)[1].str()));
			attribute.glslType = (*it)[2].str();
			attribute.name = (*it)[3].str();
			const bool known = std::ranges::any_of(attributes, [&attribute](const VertexAttribute& p_existing) {
				return p_existing.location == attribute.location;
			});
			if (!known)
			{
				attributes.push_back(std::move(attribute));
			}
		}
		std::ranges::sort(attributes, {}, &VertexAttribute::location);
		return attributes;
	}

	/** The `#define`s one variant is compiled with: the stage, then the pass, then the
	 * features. The stage one is what lets Common/Utils.glsl keep its derivative helper out
	 * of the stages that cannot call it (see ComputeTBN there). */
	/** Vertex attributes the material declares (material { vertexAttributes : [...] }) become
	 * layout qualifiers and HAS_ATTRIBUTE_* defines, the way filament's material builder turns
	 * its required attributes into shader defines. The declaration order is the attribute
	 * order of the mesh, so the same material cannot serve two different vertex layouts; that
	 * is exactly filament's model (a material owns its vertex layout). */
	std::string ShaderGenerator::AttributeDeclarations(Stage p_stage) const
	{
		struct Entry
		{
			const char* name;
			const char* glslType;
			const char* variable;
		};
		static constexpr Entry kEntries[] = {
			{ "POSITION", "vec3", "geo_Pos" },
			{ "UV0", "vec2", "geo_TexCoords" },
			{ "NORMAL", "vec3", "geo_Normal" },
			{ "TANGENT", "vec3", "geo_Tangent" },
			{ "BITANGENT", "vec3", "geo_Bitangent" },
			{ "DOMAIN_ID", "vec2", "geo_DomainId" },
		};

		std::ostringstream out;
		uint32_t location = 0;
		for (const std::string& name : m_material.vertexAttributes)
		{
			for (const Entry& entry : kEntries)
			{
				if (name == entry.name)
				{
					if (p_stage == Stage::Vertex)
					{
						out << "layout(location = " << location << ") in " << entry.glslType
							<< " " << entry.variable << ";\n";
					}
					out << "#define HAS_ATTRIBUTE_" << entry.name << "\n";
					++location;
					break;
				}
			}
		}
		return out.str();
	}

	std::string ShaderGenerator::VariantDefines(const PassVariant& p_variant, Stage p_stage, const std::string& p_stageText) const
	{
		std::ostringstream out;
		out << (p_stage == Stage::Vertex ? "#define MOON_STAGE_VERTEX\n"
			: p_stage == Stage::Fragment ? "#define MOON_STAGE_FRAGMENT\n"
			: "#define MOON_STAGE_GEOMETRY\n");
		if (!p_variant.pass.empty())
		{
			out << "#define " << p_variant.pass << "\n";
		}
		for (const std::string& feature : p_variant.features)
		{
			// A define the stage never mentions cannot change its code. Dropping it lets
			// variants that differ only in unused bits share one compiled stage text, which
			// is what keeps 128 keyed variants down to the handful with different code.
			if (!p_stageText.empty() && p_stageText.find(feature) == std::string::npos)
			{
				continue;
			}
			out << "#define " << feature << "\n";
		}
		return out.str();
	}

	/** Where the per-variant #defines go in a stage's shared text.
	 *
	 * Every variant of a material compiles the same stage text except for the `#define` lines
	 * at the top, so the package stores that text once per stage and the runtime splices the
	 * defines in - filament's DictionaryChunk idea. Without it `Standard` is 192 copies of the
	 * same 64 KB and the archive is 20 MB instead of two. */
	// kDefinesPlaceholder is declared in ShaderGenerator.h: the package writer splices it too.

	/** The (field, define) pairs the material-defines dump lists, in the order the surface
	 * model declares them; Matc.cpp asks for them instead of reaching into kInputFields. */
	std::vector<std::string> ShaderGenerator::WrittenInputDefines(const std::set<std::string>& p_writtenFields)
	{
		std::vector<std::string> defines;
		for (const InputField& field : kInputFields)
		{
			if (p_writtenFields.contains(field.name))
			{
				defines.emplace_back(field.define);
			}
		}
		return defines;
	}

	/** One complete stage source: version, defines, prelude, the entry point, main.
	 *
	 * The #line directives point back into the .shader the user wrote, so a compile error names
	 * the file and the line it came from rather than a line of this concatenation - the same
	 * job filament's Includes.cpp does for #include.
	 *
	 * A stage that defines `void main()` of its own is taken as it is: it keeps the prelude and
	 * its parameters, and nothing else is generated - no attribute declarations, no varyings,
	 * no output, no main. That is the escape hatch for the shaders whose stage is not "adjust
	 * the attributes, then the fixed transform": a billboard that rebuilds the model-view
	 * matrix, a skybox that sets w = z, a picking pass with a second colour attachment. A
	 * geometry stage is always like this - there is no skeleton for it at all.
	 *
	 * With p_shared the defines are replaced by kDefinesPlaceholder: that is the one copy of
	 * this stage the package keeps, and every variant contributes just its define lines. */
	std::string ShaderGenerator::Build(Stage p_stage, const PassVariant& p_variant, bool p_shared)
	{
		m_error.clear();

		const std::string& body = p_stage == Stage::Vertex ? m_material.vertexBody
			: p_stage == Stage::Fragment ? m_material.fragmentBody
			: m_material.geometryBody;
		const uint32_t entryLine = p_stage == Stage::Vertex ? m_material.vertexEntryLine
			: p_stage == Stage::Fragment ? m_material.fragmentEntryLine
			: m_material.geometryEntryLine;
		const bool rawStage = p_stage == Stage::Geometry || DefinesMain(body);
		const bool needsTbn = NeedsTangentFrame(m_writtenFields);

		std::ostringstream out;
		out << "#version 450 core\n"
			// The #line directives below carry a file name, which is an extension (filament
			// enables it the same way, see its CodeGenerator.cpp). Without it glslang rejects
			// the directive outright, so the build-time validation could not read what we
			// generate; drivers accept the form either way.
			<< "#extension GL_GOOGLE_cpp_style_line_directive : enable\n"
			<< "// generated by MoonMatc from " << m_fileName << ", stage " << StageName(p_stage) << '\n';
		if (p_shared)
		{
			out << kDefinesPlaceholder << '\n';
		}
		else
		{
			out << "// pass '" << p_variant.pass << "', " << p_variant.features.size()
				<< " feature(s)\n"
				<< VariantDefines(p_variant, p_stage);
		}
		out << m_prelude;
		if (!rawStage)
		{
			out << VaryingsPrelude(p_stage == Stage::Vertex, needsTbn);
		}
		out << AttributeDeclarations(p_stage);
		out << ParametersPrelude(m_material.parameters, m_material.parameterBlockSize);

		// A lit surface's fragment stage carries the shading model's code. Where it goes matters:
		//
		//  - a skeleton stage gets the whole prelude (surface stack + `evaluateMaterial`) here,
		//    before its `material()` block. Nothing in a material block may change what the
		//    prelude compiles to - `#define`s there are the material's own business;
		//  - a raw stage *is* `main()`, so it needs `evaluateSurface` declared before it. But the
		//    material block may start with `#define VARIANT_HAS_DIRECTIONAL_LIGHTING` (the CAD
		//    material did exactly that), and the surface stack tests those with `#if` at include
		//    time. Emitting the stack before the body therefore compiled both light cases out and
		//    the surface came out black. So for a raw stage: a prototype before the body, the
		//    prelude itself after it.
		const bool litSurface = p_stage == Stage::Fragment
			&& m_material.shadingModel != "unlit" && m_material.domain == "surface";
		if (litSurface)
		{
			if (rawStage)
			{
				out << "\n#line 1 \"<lit surface>\"\n"
					<< "vec4 evaluateSurface(MaterialInputs m, vec3 fragPos, vec3 geometricNormal,\n"
					<< "    vec3 shadingNormal, vec3 clearCoatNormal, vec3 v);\n";
			}
			else
			{
				out << LitSurfacePrelude(true);
			}
		}

		out << "\n#line " << entryLine << " \"" << m_fileName << "\"\n"
			<< body
			<< "\n#line 1 \"<generated>\"\n";
		if (litSurface && rawStage)
		{
			out << LitSurfacePrelude(false);
		}
		if (!rawStage)
		{
			if (p_stage == Stage::Vertex)
			{
				out << VertexSkeleton(!m_material.vertexBody.empty(), needsTbn);
			}
			else
			{
				out << FragmentSkeleton();
			}
		}

		// The material's own #includes are expanded here, into the stage that is about to be
		// compiled - there is no runtime pass left to do it once it is a package.
		std::set<std::string> visited;
		std::string expanded;
		if (!ExpandIncludes(out.str(), m_includeDirs, m_includeResolver, m_fileName, visited, expanded, m_error))
		{
			return {};
		}
		return expanded;
	}
}
