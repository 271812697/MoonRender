// MoonMatc - the offline half of the shader pipeline, the counterpart of Filament's matc.
//
// One .ovfx in, one .ovshader package out. The package holds what the runtime used to work
// out on every launch: the final GLSL of every (pass, variant, stage), the reflection of the
// material's parameters, and the variant list. A file that has not been migrated yet (no
// material { } block) is copied through untouched, so the pipeline can take the shaders one
// at a time.
//
// Usage:
//   MoonMatc -i <file.ovfx> -o <file.ovshader> [-I <include dir>] [-D NAME]...
//            [--dump-glsl <dir>] [--dump-json <file>] [-E]
//
// See docs/ShaderPipeline.md for the format and the design it copies.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	// ---------------------------------------------------------------------------------
	// Source handling
	// ---------------------------------------------------------------------------------

	std::string ReadFile(const std::filesystem::path& p_path, bool& p_ok)
	{
		std::ifstream stream(p_path, std::ios::binary);
		p_ok = static_cast<bool>(stream);
		if (!p_ok)
		{
			return {};
		}
		return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
	}

	bool WriteFile(const std::filesystem::path& p_path, const std::string& p_text)
	{
		std::error_code error;
		if (p_path.has_parent_path())
		{
			std::filesystem::create_directories(p_path.parent_path(), error);
		}
		std::ofstream stream(p_path, std::ios::binary);
		if (!stream)
		{
			std::cerr << "[MoonMatc] cannot write " << p_path << std::endl;
			return false;
		}
		stream.write(p_text.data(), static_cast<std::streamsize>(p_text.size()));
		return static_cast<bool>(stream);
	}

	std::string Trim(std::string_view p_text)
	{
		size_t begin = 0;
		size_t end = p_text.size();
		while (begin < end && std::isspace(static_cast<unsigned char>(p_text[begin]))) { ++begin; }
		while (end > begin && std::isspace(static_cast<unsigned char>(p_text[end - 1]))) { --end; }
		return std::string(p_text.substr(begin, end - begin));
	}

	std::string ToUpper(std::string p_text)
	{
		std::transform(p_text.begin(), p_text.end(), p_text.begin(),
			[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		return p_text;
	}

	/** Removes // and block comments, keeping line breaks so that the #line numbers the
	 * diagnostics show still match the file. */
	std::string StripComments(const std::string& p_text)
	{
		std::string out;
		out.reserve(p_text.size());
		bool inLine = false;
		bool inBlock = false;
		for (size_t i = 0; i < p_text.size(); ++i)
		{
			const char c = p_text[i];
			const char next = (i + 1 < p_text.size()) ? p_text[i + 1] : '\0';
			if (inLine)
			{
				if (c == '\n') { inLine = false; out += c; }
				continue;
			}
			if (inBlock)
			{
				if (c == '*' && next == '/') { inBlock = false; ++i; }
				else if (c == '\n') { out += c; }
				continue;
			}
			if (c == '/' && next == '/') { inLine = true; ++i; continue; }
			if (c == '/' && next == '*') { inBlock = true; ++i; continue; }
			out += c;
		}
		return out;
	}

	// ---------------------------------------------------------------------------------
	// The material file
	// ---------------------------------------------------------------------------------

	/** One entry of `parameters : [ { type : float3, name : u_Albedo }, ... ]`. */
	struct Parameter
	{
		std::string type;
		std::string name;
	};

	struct PassVariant
	{
		std::string pass;                  // empty for the default pass
		std::set<std::string> features;    // the #define set this variant is compiled with
	};

	struct MaterialFile
	{
		std::string name;
		std::string shadingModel = "unlit";
		/** "surface" (shaded geometry, transformed by the model matrix) or "postProcess"
		 * (a full-screen pass: the vertex stage is the quad, not the mesh). Filament splits
		 * the same two cases with MaterialBuilder::materialDomain. */
		std::string domain = "surface";
		std::map<std::string, std::string> state; // blending, culling, depthWrite, ...
		std::vector<Parameter> parameters;
		std::vector<std::string> features;        // declared feature bits
		std::vector<std::string> passes;          // extra passes, default pass implied
		std::string vertexBody;                   // the vertex { ... } block, verbatim
		std::string fragmentBody;                 // the fragment { ... } block, verbatim
		uint32_t vertexEntryLine = 1;             // where those blocks start, for #line
		uint32_t fragmentEntryLine = 1;
	};

	/** The 1-based line a fragment of the source starts on: the #line directives handed to
	 * the driver point back here, so an error names the .ovfx line the user wrote. */
	uint32_t LineOf(const std::string& p_text, const std::string& p_fragment)
	{
		const size_t at = p_text.find(p_fragment);
		if (at == std::string::npos)
		{
			return 1;
		}
		return 1u + static_cast<uint32_t>(std::count(p_text.begin(), p_text.begin() + static_cast<ptrdiff_t>(at), '\n'));
	}

	/** Finds the body of a `keyword { ... }` block, braces balanced, and returns the index
	 * just past the closing brace. Comments have already been stripped by the caller. */
	bool TakeBlock(const std::string& p_text, const std::string& p_keyword, size_t p_from,
		std::string& p_body, size_t& p_next)
	{
		const size_t at = p_text.find(p_keyword, p_from);
		if (at == std::string::npos)
		{
			return false;
		}
		const size_t open = p_text.find('{', at);
		if (open == std::string::npos)
		{
			return false;
		}
		int depth = 0;
		for (size_t i = open; i < p_text.size(); ++i)
		{
			if (p_text[i] == '{') { ++depth; }
			else if (p_text[i] == '}')
			{
				if (--depth == 0)
				{
					p_body = p_text.substr(open + 1, i - open - 1);
					p_next = i + 1;
					return true;
				}
			}
		}
		return false;
	}

	/** Splits `a : b, c : d` style material state into key/value pairs, and the parameters
	 * array into Parameter records. Only what the slice needs - a hand written scanner, in
	 * the spirit of filament-matp's lexeme readers rather than a general parser. */
	void ParseMaterialBlock(const std::string& p_body, MaterialFile& p_out)
	{
		size_t i = 0;
		const auto skipSpace = [&]() { while (i < p_body.size() && std::isspace(static_cast<unsigned char>(p_body[i]))) { ++i; } };
		const auto readName = [&]() {
			skipSpace();
			std::string name;
			while (i < p_body.size() && (std::isalnum(static_cast<unsigned char>(p_body[i])) || p_body[i] == '_' || p_body[i] == '.'))
			{
				name += p_body[i++];
			}
			return name;
		};

		while (i < p_body.size())
		{
			const std::string key = readName();
			skipSpace();
			if (key.empty())
			{
				++i;
				continue;
			}
			if (i >= p_body.size() || p_body[i] != ':')
			{
				continue;
			}
			++i; // ':'
			skipSpace();

			if (p_body[i] == '[')
			{
				// Array: parameters (objects) or a plain name list (features / passes).
				const size_t close = p_body.find(']', i);
				const std::string array = p_body.substr(i + 1, (close == std::string::npos ? p_body.size() : close) - i - 1);
				i = (close == std::string::npos) ? p_body.size() : close + 1;

				if (key == "parameters")
				{
					size_t at = 0;
					while ((at = array.find('{', at)) != std::string::npos)
					{
						const size_t end = array.find('}', at);
						if (end == std::string::npos) { break; }
						std::string object = array.substr(at + 1, end - at - 1);
						Parameter parameter;
						const size_t typeAt = object.find("type");
						const size_t nameAt = object.find("name");
						if (typeAt != std::string::npos)
						{
							const size_t colon = object.find(':', typeAt);
							const size_t comma = object.find(',', colon);
							parameter.type = Trim(object.substr(colon + 1, comma - colon - 1));
						}
						if (nameAt != std::string::npos)
						{
							const size_t colon = object.find(':', nameAt);
							parameter.name = Trim(object.substr(colon + 1));
						}
						if (!parameter.name.empty())
						{
							p_out.parameters.push_back(parameter);
						}
						at = end + 1;
					}
				}
				else
				{
					std::string item;
					std::istringstream stream(array);
					while (std::getline(stream, item, ','))
					{
						const std::string trimmed = Trim(item);
						if (trimmed.empty()) { continue; }
						if (key == "features" || key == "variantFilter") { p_out.features.push_back(Trim(trimmed)); }
						else if (key == "passes") { p_out.passes.push_back(Trim(trimmed)); }
					}
				}
				continue;
			}

			// Scalar value: up to the next ',' at this level.
			const size_t end = p_body.find(',', i);
			const std::string value = Trim(p_body.substr(i, (end == std::string::npos ? p_body.size() : end) - i));
			i = (end == std::string::npos) ? p_body.size() : end + 1;

			if (key == "name") { p_out.name = value; }
			else if (key == "shadingModel") { p_out.shadingModel = value; }
			else if (key == "domain") { p_out.domain = value; }
			else { p_out.state[key] = value; }
		}
	}

	bool ParseMaterialFile(const std::string& p_text, MaterialFile& p_out, std::string& p_error)
	{
		const std::string text = StripComments(p_text);

		std::string materialBody;
		size_t next = 0;
		if (!TakeBlock(text, "material", 0, materialBody, next))
		{
			p_error = "no material { } block";
			return false;
		}
		ParseMaterialBlock(materialBody, p_out);

		std::string vertexBody;
		size_t vertexNext = 0;
		if (TakeBlock(text, "vertex", next, vertexBody, vertexNext))
		{
			p_out.vertexBody = Trim(vertexBody);
			p_out.vertexEntryLine = LineOf(text, p_out.vertexBody);
			next = vertexNext;
		}
		std::string fragmentBody;
		size_t fragmentNext = 0;
		if (TakeBlock(text, "fragment", next, fragmentBody, fragmentNext))
		{
			p_out.fragmentBody = Trim(fragmentBody);
			p_out.fragmentEntryLine = LineOf(text, p_out.fragmentBody);
		}

		if (p_out.name.empty())
		{
			p_error = "material { name : ... } is required";
			return false;
		}
		if (p_out.fragmentBody.empty())
		{
			p_error = "a fragment { } block is required";
			return false;
		}
		return true;
	}

	// ---------------------------------------------------------------------------------
	// Code generation: prelude + entry points + main
	// ---------------------------------------------------------------------------------

	/** The MaterialInputs the material() entry point writes, the function that fills them
	 * with defaults, and the function that turns them into a colour.
	 *
	 * Names and shape follow filament: `MaterialInputs inputs; initMaterial(inputs);
	 * material(inputs); fragColor = evaluateMaterial(inputs);` is exactly what its generated
	 * surface_main.fs does (shaders/src/surface_main.fs:50-56), and the fields are the ones
	 * surface_material_inputs.fs declares, cut down to what this engine shades with. */
	std::string MaterialInputsPrelude(const std::string& p_shadingModel)
	{
		const bool lit = (p_shadingModel != "unlit");
		std::ostringstream out;
		out << "struct MaterialInputs {\n"
			<< "    vec4 baseColor;\n";
		if (lit)
		{
			out << "    float roughness;\n"
				<< "    float metallic;\n"
				<< "    float reflectance;\n"
				<< "    float ambientOcclusion;\n";
		}
		out << "    vec4 emissive;\n"
			<< "    vec3 normal;\n"
			<< "};\n\n"
			<< "void initMaterial(out MaterialInputs m) {\n"
			<< "    m.baseColor = vec4(1.0);\n";
		if (lit)
		{
			out << "    m.roughness = 1.0;\n"
				<< "    m.metallic = 0.0;\n"
				<< "    m.reflectance = 0.5;\n"
				<< "    m.ambientOcclusion = 1.0;\n";
		}
		out << "    m.emissive = vec4(0.0);\n"
			<< "    m.normal = vec3(0.0, 0.0, 1.0);\n"
			<< "}\n\n"
			// The .mat files call it prepareMaterial(); keeping the alias means a shader
			// written either way compiles.
			<< "void prepareMaterial(out MaterialInputs m) { initMaterial(m); }\n\n"
			// The shading model's verdict on the inputs. Unlit is the whole slice.
			<< (lit
				? "vec4 evaluateMaterial(inout MaterialInputs m);\n"
				: "vec4 evaluateMaterial(inout MaterialInputs m) { return m.baseColor + m.emissive; }\n");
		return out.str();
	}

	/** The varyings both stages agree on. Part of the prelude - the material's entry point
	 * may read them, so they have to be declared before it, which is what filament does in
	 * surface_varyings.glsl (and what post_process_main.vs/.fs pass instead of them). */
	std::string VaryingsPrelude(bool p_vertex, const std::string& p_domain)
	{
		std::ostringstream out;
		if (p_domain == "postProcess")
		{
			// A full-screen pass only carries texture coordinates between the stages.
			out << (p_vertex ? "out vec2 TexCoords;\n\n" : "in vec2 TexCoords;\n\n");
			return out.str();
		}
		out << (p_vertex ? "out VS_OUT {\n" : "in VS_OUT {\n")
			<< "    vec3 FragPos;\n"
			<< "    vec2 TexCoords;\n"
			<< "    vec3 Normal;\n"
			<< (p_vertex ? "} vs_out;\n\n" : "} fs_in;\n\n");
		return out.str();
	}

	/** The vertex main: the transform the entry point is not allowed to touch. Copied from
	 * what Unlit/Standard.ovfx do by hand today, and from filament's post_process_main.vs for
	 * the full-screen case. */
	std::string VertexSkeleton(bool p_hasUserVertex, const std::string& p_domain)
	{
		std::ostringstream out;
		if (p_domain == "postProcess")
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
			<< "layout (location = 2) in vec3 geo_Normal;\n\n";
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
			<< "    vs_out.Normal = mat3(ubo_Model) * normal;\n"
			<< "    gl_Position = ubo_Projection * ubo_View * vec4(vs_out.FragPos, 1.0);\n"
			<< "}\n";
		return out.str();
	}

	/** The fragment main, copied in shape from filament's surface_main.fs:50-56: fill the
	 * inputs with defaults, run the material's entry point, then let the shading model turn
	 * them into a colour. */
	std::string FragmentSkeleton()
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

	/** The GLSL name of a parameter type. The material files spell them the way filament's
	 * DSL does (float4, int3, ...); GLSL calls the same things vec4, ivec3, and sampling one
	 * a name wrong is a compile error rather than a silent default. */
	std::string GlslType(const std::string& p_type)
	{
		static const std::map<std::string, std::string> kTypes = {
			{ "bool", "bool" }, { "bool2", "bvec2" }, { "bool3", "bvec3" }, { "bool4", "bvec4" },
			{ "int", "int" }, { "int2", "ivec2" }, { "int3", "ivec3" }, { "int4", "ivec4" },
			{ "uint", "uint" }, { "uint2", "uvec2" }, { "uint3", "uvec3" }, { "uint4", "uvec4" },
			{ "float", "float" }, { "float2", "vec2" }, { "float3", "vec3" }, { "float4", "vec4" },
			{ "mat2", "mat2" }, { "mat3", "mat3" }, { "mat4", "mat4" },
			{ "sampler2D", "sampler2D" }, { "sampler2DArray", "sampler2DArray" },
			{ "samplerCube", "samplerCube" }, { "sampler3D", "sampler3D" },
		};
		const auto it = kTypes.find(p_type);
		return it != kTypes.end() ? it->second : p_type;
	}

	/** The material's parameters, as the uniforms the entry point reads.
	 *
	 * Filament packs them into a generated `materialParams` block; this engine keeps plain
	 * `uniform` declarations, so the names stay the `u_...` the C++ side already sets and no
	 * call site has to change (see docs/ShaderPipeline.md, section 8). Declared for both
	 * stages - a parameter only one of them reads is dropped by the compiler. */
	std::string ParametersPrelude(const std::vector<Parameter>& p_parameters)
	{
		std::ostringstream out;
		for (const Parameter& parameter : p_parameters)
		{
			out << "uniform " << GlslType(parameter.type) << " " << parameter.name << ";\n";
		}
		out << "\n";
		return out.str();
	}

	/** The `#define`s one variant is compiled with: the pass, then the features. */
	std::string VariantDefines(const PassVariant& p_variant)
	{
		std::ostringstream out;
		if (!p_variant.pass.empty())
		{
			out << "#define " << p_variant.pass << "\n";
		}
		for (const std::string& feature : p_variant.features)
		{
			out << "#define " << feature << "\n";
		}
		return out.str();
	}

	/** One complete stage source: version, defines, prelude, the entry point, main.
	 *
	 * The #line directives point back into the .ovfx the user wrote, so a compile error names
	 * the file and the line it came from rather than a line of this concatenation - the same
	 * job filament's Includes.cpp does for #include. */
	std::string BuildStage(
		bool p_vertex,
		const MaterialFile& p_material,
		const PassVariant& p_variant,
		const std::string& p_prelude,
		const std::string& p_fileName,
		uint32_t p_entryLine)
	{
		std::ostringstream out;
		out << "#version 450 core\n"
			<< "// generated by MoonMatc from " << p_fileName
			<< " (pass '" << p_variant.pass << "', "
			<< p_variant.features.size() << " feature(s))\n"
			<< VariantDefines(p_variant)
			<< p_prelude
			<< VaryingsPrelude(p_vertex, p_material.domain)
			<< ParametersPrelude(p_material.parameters)
			<< "\n#line " << p_entryLine << " \"" << p_fileName << "\"\n"
			<< (p_vertex ? p_material.vertexBody : p_material.fragmentBody)
			<< "\n#line 1 \"<generated>\"\n"
			<< (p_vertex ? VertexSkeleton(!p_material.vertexBody.empty(), p_material.domain)
				: FragmentSkeleton());
		return out.str();
	}

	// ---------------------------------------------------------------------------------
	// The package: 8 byte tags, like filabridge/include/filament/MaterialChunkType.h
	// ---------------------------------------------------------------------------------

	constexpr char kMagic[8] = { 'M', 'O', 'O', 'N', 'S', 'D', 'R', '1' };

	void AppendChunk(std::string& p_out, const char* p_tag, const std::string& p_payload)
	{
		p_out.append(p_tag, 8);
		const uint32_t size = static_cast<uint32_t>(p_payload.size());
		p_out.append(reinterpret_cast<const char*>(&size), sizeof(size));
		p_out.append(p_payload);
	}

	uint64_t HashOf(const std::string& p_text)
	{
		uint64_t hash = 1469598103934665603ull; // FNV-1a
		for (unsigned char c : p_text)
		{
			hash ^= c;
			hash *= 1099511628211ull;
		}
		return hash;
	}
}

int main(int argc, char* argv[])
{
	std::string inputPath;
	std::string outputPath;
	std::string dumpGlslDir;
	std::string dumpJsonPath;
	std::vector<std::string> includeDirs;
	std::vector<std::string> extraDefines;
	bool preprocessOnly = false;

	for (int i = 1; i < argc; ++i)
	{
		const std::string argument = argv[i];
		const auto value = [&](std::string& p_target) {
			if (i + 1 < argc) { p_target = argv[++i]; }
		};
		if (argument == "-i") { value(inputPath); }
		else if (argument == "-o") { value(outputPath); }
		else if (argument == "-I") { std::string dir; value(dir); includeDirs.push_back(dir); }
		else if (argument == "-D") { std::string define; value(define); extraDefines.push_back(define); }
		else if (argument == "--dump-glsl") { value(dumpGlslDir); }
		else if (argument == "--dump-json") { value(dumpJsonPath); }
		else if (argument == "-E") { preprocessOnly = true; }
		else if (!argument.empty() && argument[0] == '-')
		{
			std::cerr << "[MoonMatc] unknown option " << argument << std::endl;
			return 1;
		}
	}

	if (inputPath.empty() || outputPath.empty())
	{
		std::cerr << "Usage: MoonMatc -i <file.ovfx> -o <file.ovshader> "
			<< "[-I dir]... [-D NAME]... [--dump-glsl dir] [--dump-json file] [-E]" << std::endl;
		return 1;
	}

	bool ok = false;
	const std::string text = ReadFile(inputPath, ok);
	if (!ok)
	{
		std::cerr << "[MoonMatc] cannot read " << inputPath << std::endl;
		return 1;
	}

	MaterialFile material;
	std::string error;
	if (!ParseMaterialFile(text, material, error))
	{
		// Not a material file yet (the shaders are migrated one at a time): copy it through
		// byte for byte, which is exactly what the pipeline did before MoonMatc existed.
		std::cout << "[MoonMatc] " << inputPath << ": passthrough (" << error << ")" << std::endl;
		return WriteFile(outputPath, text) ? 0 : 1;
	}

	// Variants: the power set of the declared features, one entry per pass. (Filament uses a
	// fixed enumeration instead - see docs/ShaderPipeline.md, this is the temporary shape.)
	std::vector<PassVariant> variants;
	std::vector<std::string> passes;
	passes.emplace_back();
	for (const std::string& pass : material.passes) { passes.push_back(pass); }
	const size_t featureCount = material.features.size();
	for (const std::string& pass : passes)
	{
		for (size_t mask = 0; mask < (size_t{ 1 } << featureCount); ++mask)
		{
			PassVariant variant;
			variant.pass = pass;
			for (size_t bit = 0; bit < featureCount; ++bit)
			{
				if (mask & (size_t{ 1 } << bit)) { variant.features.insert(material.features[bit]); }
			}
			variants.push_back(std::move(variant));
		}
	}

	const std::filesystem::path fileName = std::filesystem::path(inputPath).filename();
	const std::string nameForLines = fileName.string();

	// Prelude: the buffer and utility headers the engine already has, then the generated
	// MaterialInputs. Everything the shader used to #include by hand.
	std::string commonPrelude;
	for (const std::string& relative : { "Common/Buffers/EngineUBO.ovfxh", "Common/Utils.ovfxh" })
	{
		for (const std::string& dir : includeDirs)
		{
			bool found = false;
			const std::string candidate = (std::filesystem::path(dir) / relative).string();
			const std::string content = ReadFile(candidate, found);
			if (found)
			{
				commonPrelude += "\n#line 1 \"" + relative + "\"\n" + content + "\n";
				break;
			}
		}
	}
	commonPrelude += "\n#line 1 \"<material inputs>\"\n" + MaterialInputsPrelude(material.shadingModel);

	// Entries: "pass|features|stage" -> GLSL, and the reflection table.
	std::map<std::string, std::string> blobs;
	std::vector<std::string> blobOrder;
	for (const PassVariant& variant : variants)
	{
		std::string featureKey;
		for (const std::string& feature : variant.features)
		{
			featureKey += (featureKey.empty() ? "" : "|") + feature;
		}
		const std::string prefix = variant.pass + "/" + featureKey + "/";
		blobs[prefix + "vertex"] = BuildStage(
			true, material, variant, commonPrelude, nameForLines, material.vertexEntryLine);
		blobs[prefix + "fragment"] = BuildStage(
			false, material, variant, commonPrelude, nameForLines, material.fragmentEntryLine);
		blobOrder.push_back(prefix + "vertex");
		blobOrder.push_back(prefix + "fragment");
	}

	// Reflection: the parameters this material exposes, in the shape a panel (or a material
	// editor) can read straight away.
	std::ostringstream json;
	json << "{\n  \"name\": \"" << material.name << "\",\n"
		<< "  \"shadingModel\": \"" << material.shadingModel << "\",\n"
		<< "  \"source\": \"" << nameForLines << "\",\n"
		<< "  \"variants\": [\n";
	for (size_t i = 0; i < variants.size(); ++i)
	{
		json << "    { \"pass\": \"" << variants[i].pass << "\", \"features\": [";
		size_t f = 0;
		for (const std::string& feature : variants[i].features)
		{
			json << (f++ == 0 ? "\"" : ", \"") << feature << "\"";
		}
		json << "] }" << (i + 1 < variants.size() ? "," : "") << "\n";
	}
	json << "  ],\n  \"parameters\": [\n";
	for (size_t i = 0; i < material.parameters.size(); ++i)
	{
		json << "    { \"name\": \"" << material.parameters[i].name
			<< "\", \"type\": \"" << material.parameters[i].type << "\" }"
			<< (i + 1 < material.parameters.size() ? "," : "") << "\n";
	}
	json << "  ]\n}\n";

	if (!dumpJsonPath.empty() && !WriteFile(dumpJsonPath, json.str()))
	{
		return 1;
	}

	if (!dumpGlslDir.empty())
	{
		for (const std::string& key : blobOrder)
		{
			std::string fileNameOut = key;
			std::replace(fileNameOut.begin(), fileNameOut.end(), '/', '_');
			std::replace(fileNameOut.begin(), fileNameOut.end(), '|', '-');
			if (!WriteFile(std::filesystem::path(dumpGlslDir) / (fileNameOut + ".glsl"), blobs[key]))
			{
				return 1;
			}
		}
	}

	if (preprocessOnly)
	{
		std::cout << blobs[blobOrder.front()] << std::endl;
		return 0;
	}

	// The package itself.
	std::string package;
	package.append(kMagic, sizeof(kMagic));

	// SHDRGLSL: the blob dictionary - a count, then (key length, key, size, bytes) per blob.
	{
		std::string payload;
		const uint32_t count = static_cast<uint32_t>(blobOrder.size());
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const std::string& key : blobOrder)
		{
			const uint32_t keySize = static_cast<uint32_t>(key.size());
			const std::string& source = blobs[key];
			const uint32_t size = static_cast<uint32_t>(source.size());
			payload.append(reinterpret_cast<const char*>(&keySize), sizeof(keySize));
			payload.append(key);
			payload.append(reinterpret_cast<const char*>(&size), sizeof(size));
			payload.append(source);
		}
		AppendChunk(package, "SHDRGLSL", payload);
	}

	// SHDRVARI: the variant list, so the runtime can reject a combination the build never made.
	{
		std::string payload;
		const uint32_t count = static_cast<uint32_t>(variants.size());
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const PassVariant& variant : variants)
		{
			const uint32_t passSize = static_cast<uint32_t>(variant.pass.size());
			payload.append(reinterpret_cast<const char*>(&passSize), sizeof(passSize));
			payload.append(variant.pass);
			const uint32_t featureCountOut = static_cast<uint32_t>(variant.features.size());
			payload.append(reinterpret_cast<const char*>(&featureCountOut), sizeof(featureCountOut));
			for (const std::string& feature : variant.features)
			{
				const uint32_t size = static_cast<uint32_t>(feature.size());
				payload.append(reinterpret_cast<const char*>(&size), sizeof(size));
				payload.append(feature);
			}
		}
		AppendChunk(package, "SHDRVARI", payload);
	}

	// SHDRPARM: name/type pairs of the material parameters (the binary form of the JSON).
	{
		std::string payload;
		const uint32_t count = static_cast<uint32_t>(material.parameters.size());
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const Parameter& parameter : material.parameters)
		{
			const uint32_t nameSize = static_cast<uint32_t>(parameter.name.size());
			const uint32_t typeSize = static_cast<uint32_t>(parameter.type.size());
			payload.append(reinterpret_cast<const char*>(&nameSize), sizeof(nameSize));
			payload.append(parameter.name);
			payload.append(reinterpret_cast<const char*>(&typeSize), sizeof(typeSize));
			payload.append(parameter.type);
		}
		AppendChunk(package, "SHDRPARM", payload);
	}

	// SHDRSTAT: the material state (blending, culling, ...) the material block declared.
	{
		std::string payload;
		const uint32_t count = static_cast<uint32_t>(material.state.size());
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const auto& [key, value] : material.state)
		{
			const uint32_t keySize = static_cast<uint32_t>(key.size());
			const uint32_t valueSize = static_cast<uint32_t>(value.size());
			payload.append(reinterpret_cast<const char*>(&keySize), sizeof(keySize));
			payload.append(key);
			payload.append(reinterpret_cast<const char*>(&valueSize), sizeof(valueSize));
			payload.append(value);
		}
		AppendChunk(package, "SHDRSTAT", payload);
	}

	// SHDRNAME / SHDRUUID: what this package is, and whether it still matches its sources.
	AppendChunk(package, "SHDRNAME", material.name);
	{
		const uint64_t hash = HashOf(text);
		std::string payload(reinterpret_cast<const char*>(&hash), sizeof(hash));
		AppendChunk(package, "SHDRUUID", payload);
	}
	{
		const char version[] = "1";
		AppendChunk(package, "SHDRVERS", version);
	}

	if (!WriteFile(outputPath, package))
	{
		return 1;
	}

	std::cout << "[MoonMatc] " << inputPath << " -> " << outputPath
		<< " (" << material.name << ", " << variants.size() << " variant(s), "
		<< package.size() << " bytes)" << std::endl;
	return 0;
}
