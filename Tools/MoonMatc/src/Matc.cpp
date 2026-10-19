// MoonMatc - the offline half of the shader pipeline, the counterpart of Filament's matc.
//
// One .mat in, one .ovshader package out. The package holds what the runtime used to work
// out on every launch: the final GLSL of every (pass, variant, stage), the reflection of the
// material's parameters, and the variant list. A file that has not been migrated yet (no
// material { } block) is copied through untouched, so the pipeline can take the shaders one
// at a time.
//
// This file is the library: parsing, variant expansion, code generation and the package.
// main.cpp is the command line over it, and the editor calls CompileSource directly to hot
// reload a material it is editing.
//
// See docs/ShaderPipeline.md for the format and the design it copies.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Matc.h"

namespace Moon::Matc
{
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

	/** A quoted material value ("Base Color") without its quotes; an unquoted one is returned
	 * as it is. Labels have spaces in them, so they are the fields that get quoted. */
	std::string Unquote(const std::string& p_text)
	{
		if (p_text.size() >= 2 && p_text.front() == '"' && p_text.back() == '"')
		{
			return p_text.substr(1, p_text.size() - 2);
		}
		return p_text;
	}

	/** A numeric value out of the material file. False when the text is not a whole number, in
	 * which case the designer field keeps its default. */
	bool ParseFloatText(const std::string& p_text, float& p_value)
	{
		if (p_text.empty())
		{
			return false;
		}
		char* end = nullptr;
		p_value = std::strtof(p_text.c_str(), &end);
		return end != p_text.c_str() && *end == '\0';
	}

	std::string JsonEscape(const std::string& p_text)
	{
		std::string out;
		out.reserve(p_text.size());
		for (const char c : p_text)
		{
			if (c == '"' || c == '\\')
			{
				out += '\\';
			}
			out += c;
		}
		return out;
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

	const char* StageName(Stage p_stage)
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
		/** The std140 size of the per-material uniform block: what the runtime allocates and
		 * uploads, and what the generated GLSL's block must describe. 0 when every parameter is
		 * a sampler (or there are none). */
		uint32_t parameterBlockSize = 0;
		std::vector<std::string> features;        // declared feature bits
		std::vector<std::string> passes;          // extra passes, default pass implied
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

	/** The 1-based line a fragment of the source starts on: the #line directives handed to
	 * the driver point back here, so an error names the .mat line the user wrote. */
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
					// One field of a parameter object: `field : value`, the value ending at the
					// comma that separates it from the next field. The key has to be a whole
					// word, so that "count" does not match inside `name : u_Count`.
					const auto valueOf = [&array](const std::string& p_object, const std::string& p_field) -> std::string {
						size_t at = p_object.find(p_field);
						while (at != std::string::npos)
						{
							const size_t after = at + p_field.size();
							const size_t colon = p_object.find(':', after);
							const bool wordStart = at == 0
								|| !std::isalnum(static_cast<unsigned char>(p_object[at - 1]))
								&& p_object[at - 1] != '_';
							if (wordStart && colon != std::string::npos && Trim(p_object.substr(after, colon - after)).empty())
							{
								// The value ends at the comma that separates it from the next
								// field - but a `default : vec4(1, 2, 3, 4)` has commas of its
								// own, so the ones inside brackets do not count.
								size_t end = colon + 1;
								int depth = 0;
								for (; end < p_object.size(); ++end)
								{
									const char c = p_object[end];
									if (c == '(' || c == '[') { ++depth; }
									else if (c == ')' || c == ']') { --depth; }
									else if (c == ',' && depth == 0) { break; }
								}
								return Trim(p_object.substr(colon + 1, end - colon - 1));
							}
							at = p_object.find(p_field, at + 1);
						}
						return {};
					};

					size_t at = 0;
					while ((at = array.find('{', at)) != std::string::npos)
					{
						const size_t end = array.find('}', at);
						if (end == std::string::npos) { break; }
						const std::string object = array.substr(at + 1, end - at - 1);
						Parameter parameter;
						parameter.type = valueOf(object, "type");
						parameter.name = valueOf(object, "name");
						parameter.count = valueOf(object, "count");
						parameter.defaultValue = valueOf(object, "default");

						// The designer fields. "min"/"max"/"step" are only ranges when both ends
						// are numbers; "ui : false" hides engine-owned parameters (the shadow
						// matrix, the environment map) from the material designer.
						parameter.label = Unquote(valueOf(object, "label"));
						parameter.group = Unquote(valueOf(object, "group"));
						parameter.color = valueOf(object, "color") == "true";
						parameter.ui = valueOf(object, "ui") != "false";
						parameter.hasRange = ParseFloatText(valueOf(object, "min"), parameter.min)
							&& ParseFloatText(valueOf(object, "max"), parameter.max);
						parameter.hasStep = ParseFloatText(valueOf(object, "step"), parameter.step);

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
		size_t afterVertex = 0;
		if (TakeBlock(text, "vertex", next, vertexBody, vertexNext))
		{
			p_out.vertexBody = Trim(vertexBody);
			p_out.vertexEntryLine = LineOf(text, p_out.vertexBody);
			next = vertexNext;
			afterVertex = vertexNext;
		}
		std::string fragmentBody;
		size_t fragmentNext = 0;
		if (TakeBlock(text, "fragment", next, fragmentBody, fragmentNext))
		{
			p_out.fragmentBody = Trim(fragmentBody);
			p_out.fragmentEntryLine = LineOf(text, p_out.fragmentBody);
			next = fragmentNext;
		}

		// The geometry stage: our extension, and the old files put it between the vertex and
		// the fragment sections - so it is searched from the end of the vertex block, not from
		// wherever the fragment block ended. The legacy loader's marker is spelled
		// "#shader geomerty", so the block keyword is accepted either way.
		std::string geometryBody;
		size_t geometryNext = 0;
		if (TakeBlock(text, "geometry", afterVertex, geometryBody, geometryNext)
			|| TakeBlock(text, "geomerty", afterVertex, geometryBody, geometryNext))
		{
			p_out.geometryBody = Trim(geometryBody);
			p_out.geometryEntryLine = LineOf(text, p_out.geometryBody);
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
	// #include
	//
	// The runtime used to expand these (ShaderLoader's AssembleSource, the engine's answer
	// to filament's Includes.cpp). Once a shader is a package there is no runtime pass left
	// to do it - the GLSL goes straight to the driver - so the expansion moves here, where
	// it also gets the chance to drop the two headers the prelude already carries.
	// ---------------------------------------------------------------------------------

	/** The headers every generated stage already has: a material that includes one of them
	 * would declare the same UBO or helper twice. */
	bool IsPreludeHeader(const std::string& p_relative)
	{
		return p_relative == "Common/Buffers/EngineUBO.glsl"
			|| p_relative == "Common/Utils.glsl";
	}

	struct ResolvedInclude
	{
		std::filesystem::path path;  // the file on disk
		std::string relative;        // how #line should name it
	};

	/** Where one #include points, the way the engine's path parser resolves it: a leading
	 * ':' marks an engine path, and the directories handed in with -I are the shader roots
	 * the ":Shaders/" prefix stands for. A relative include is tried against them too, which
	 * is what a file inside the tree gets for free. */
	bool ResolveInclude(const std::string& p_target, const std::vector<std::string>& p_dirs, ResolvedInclude& p_out)
	{
		std::string target = p_target;
		if (!target.empty() && target.front() == ':')
		{
			target.erase(target.begin());
		}
		std::replace(target.begin(), target.end(), '\\', '/');

		std::string withoutRoot = target;
		constexpr std::string_view kRoot = "Shaders/";
		if (withoutRoot.starts_with(kRoot))
		{
			withoutRoot.erase(0, kRoot.size());
		}

		for (const std::string& dir : p_dirs)
		{
			for (const std::string& candidate : { withoutRoot, target })
			{
				const std::filesystem::path path = std::filesystem::path(dir) / candidate;
				std::error_code error;
				if (std::filesystem::is_regular_file(path, error))
				{
					p_out.path = path;
					p_out.relative = candidate;
					return true;
				}
			}
		}
		return false;
	}

	/** Expands the #include lines of one stage and keeps the diagnostic anchors straight: an
	 * included file opens with its own "#line 1", and the line after it goes back to the file
	 * (and the line) the #include was written on.
	 *
	 * Each file is expanded once per stage - the headers have no include guards, and PBR.glsl
	 * reaches Utils.glsl a second time - and the two the prelude already carries are dropped
	 * instead of duplicated. */
	bool ExpandIncludes(
		const std::string& p_text,
		const std::vector<std::string>& p_dirs,
		const std::string& p_origin,
		std::set<std::string>& p_visited,
		std::string& p_out,
		std::string& p_error)
	{
		std::string currentFile = p_origin;
		uint32_t nextLine = 1;

		std::istringstream stream(p_text);
		std::string line;
		while (std::getline(stream, line))
		{
			const std::string trimmed = Trim(line);
			if (trimmed.starts_with("#line "))
			{
				// "#line N file": the line after it is line N of that file.
				p_out += line;
				p_out += '\n';
				// The number after "#line " is the line the one after it will be.
				size_t at = 6;
				while (at < trimmed.size() && trimmed[at] == ' ') { ++at; }
				uint32_t number = 0;
				bool hasDigits = false;
				while (at < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[at])))
				{
					number = number * 10 + static_cast<uint32_t>(trimmed[at] - '0');
					++at;
					hasDigits = true;
				}
				if (hasDigits)
				{
					nextLine = number;
				}
				const size_t first = trimmed.find('"');
				const size_t last = trimmed.rfind('"');
				if (first != std::string::npos && last != std::string::npos && last > first)
				{
					currentFile = trimmed.substr(first + 1, last - first - 1);
				}
				continue;
			}

			if (trimmed.starts_with("#include"))
			{
				const size_t first = trimmed.find('"');
				const size_t last = trimmed.rfind('"');
				if (first == std::string::npos || last == std::string::npos || last <= first)
				{
					p_error = "malformed #include in " + currentFile + " (line " + std::to_string(nextLine) + ")";
					return false;
				}

				const std::string target = trimmed.substr(first + 1, last - first - 1);
				ResolvedInclude resolved;
				if (!ResolveInclude(target, p_dirs, resolved))
				{
					p_error = "cannot resolve #include \"" + target + "\" (" + currentFile
						+ ", line " + std::to_string(nextLine) + ")";
					return false;
				}

				// Consumed either way: the include line itself occupies a line of currentFile.
				++nextLine;

				if (IsPreludeHeader(resolved.relative) || p_visited.contains(resolved.relative))
				{
					// Already in this stage, as part of the prelude or from an earlier
					// #include: dropping the line is what keeps the declarations single.
					continue;
				}
				p_visited.insert(resolved.relative);

				bool readOk = false;
				const std::string content = ReadFile(resolved.path, readOk);
				if (!readOk)
				{
					p_error = "cannot read \"" + resolved.path.string() + "\"";
					return false;
				}

				std::string expanded;
				if (!ExpandIncludes(content, p_dirs, resolved.relative, p_visited, expanded, p_error))
				{
					return false;
				}

				p_out += "#line 1 \"" + resolved.relative + "\"\n";
				p_out += expanded;
				if (!expanded.empty() && expanded.back() != '\n')
				{
					p_out += '\n';
				}
				p_out += "#line " + std::to_string(nextLine) + " \"" + currentFile + "\"\n";
				continue;
			}

			p_out += line;
			p_out += '\n';
			++nextLine;
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

	/** The feature a geometry body's leading guard is about, if it is a plain single-feature
	 * test: `#ifdef F` / `#if defined(F)` / `#if F` are +1, the negations are -1, and anything
	 * fancier (`||`, `&&`, comparisons) is 0 "do not try to be clever". */
	int GeometryGuardFeature(const std::string& p_body, std::string& p_feature)
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
	bool GuardWrapsBlock(const std::string& p_body)
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
	bool WantsGeometryStage(const std::string& p_body, const PassVariant& p_variant)
	{
		std::string feature;
		const int sense = GeometryGuardFeature(p_body, feature);
		if (sense == 0 || !GuardWrapsBlock(p_body))
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
	bool DefinesMain(const std::string& p_body)
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
	 * what Unlit/Standard.mat do by hand today, and from filament's post_process_main.vs for
	 * the full-screen case.
	 *
	 * Only generated for a stage that does not own its main() - see BuildStage. */
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
	 * them into a colour. Only generated for a stage that does not own its main(). */
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

	/** Whether a parameter type is one of the sampler types: the slot a texture goes into,
	 * as opposed to a plain uniform. */
	bool IsSamplerType(const std::string& p_dslType)
	{
		return p_dslType.starts_with("sampler")
			|| p_dslType.starts_with("isampler")
			|| p_dslType.starts_with("usampler");
	}

	/** One vertex attribute a generated stage declares: `layout (location = N) in T name;`.
	 * Read back out of the stage text rather than tracked while emitting, so a raw stage that
	 * declares its own inputs (GizmoLine's aPositionSize/aColor, the instanced ones) shows up
	 * exactly like the generated skeleton does. filament records the same pairs in its
	 * MaterialAttributeInfo chunk. */
	struct VertexAttribute
	{
		uint32_t location = 0;
		std::string glslType;
		std::string name;
	};

	std::vector<VertexAttribute> CollectVertexAttributes(const std::string& p_stageText)
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

	/** The std140 size and base alignment of a parameter type. Samplers never reach this:
	 * they are not allowed inside a uniform block and stay separate uniforms. */
	struct TypeLayout
	{
		uint32_t size;
		uint32_t alignment;
	};

	TypeLayout ParameterTypeLayout(const std::string& p_type)
	{
		static const std::map<std::string, TypeLayout> kTypes = {
			{ "bool", { 4, 4 } }, { "bool2", { 8, 8 } }, { "bool3", { 12, 16 } }, { "bool4", { 16, 16 } },
			{ "int", { 4, 4 } }, { "int2", { 8, 8 } }, { "int3", { 12, 16 } }, { "int4", { 16, 16 } },
			{ "uint", { 4, 4 } }, { "uint2", { 8, 8 } }, { "uint3", { 12, 16 } }, { "uint4", { 16, 16 } },
			{ "float", { 4, 4 } }, { "float2", { 8, 8 } }, { "float3", { 12, 16 } }, { "float4", { 16, 16 } },
			{ "mat2", { 32, 8 } }, { "mat3", { 48, 16 } }, { "mat4", { 64, 16 } },
		};
		const auto it = kTypes.find(p_type);
		return it != kTypes.end() ? it->second : TypeLayout{ 4, 4 };
	}

	uint32_t RoundUp(uint32_t p_value, uint32_t p_alignment)
	{
		return (p_value + p_alignment - 1) / p_alignment * p_alignment;
	}

	/** Assigns the std140 offsets of the parameter block and the texture units of the samplers.
	 *
	 * The block holds every non-sampler parameter in declaration order (engine-owned `ui:false`
	 * ones included: they are still uniforms). The generated GLSL declares the same block with
	 * `binding = 2`, so the driver's std140 layout has to agree with this computation; the
	 * runtime validates the offsets once when it links a program (see ShaderLoader). */
	void LayoutParameters(MaterialFile& p_material)
	{
		uint32_t offset = 0;
		uint32_t samplerBinding = 0;

		for (Parameter& parameter : p_material.parameters)
		{
			if (IsSamplerType(parameter.type))
			{
				parameter.binding = samplerBinding++;
				continue;
			}

			const TypeLayout layout = ParameterTypeLayout(parameter.type);
			const uint32_t count = parameter.count.empty()
				? 1u : static_cast<uint32_t>(std::stoul(parameter.count));

			if (count > 1)
			{
				// std140 arrays round the element size (and therefore the stride) up to 16.
				offset = RoundUp(offset, 16);
				parameter.offset = offset;
				parameter.stride = RoundUp(layout.size, 16);
				offset += parameter.stride * count;
			}
			else
			{
				offset = RoundUp(offset, layout.alignment);
				parameter.offset = offset;
				parameter.stride = layout.size;
				offset += layout.size;
			}
		}

		p_material.parameterBlockSize = RoundUp(offset, 16);
	}

	/** The material's parameters, as the descriptors the entry point reads.
	 *
	 * The non-sampler parameters are one std140 uniform block - the engine's per-material
	 * descriptor: a single UBO at binding point 2, which a material instance fills by offset.
	 * Samplers cannot be block members, so they stay separate uniforms with a fixed texture unit
	 * each (`layout(binding = N)`), the same idea applied to the sampler descriptors. Declared
	 * for both stages - a parameter only one of them reads is dropped by the compiler.
	 *
	 * The declared initialisers move to the runtime: a block member cannot have one, and the
	 * material's property defaults (parsed from the same declarations, see SHDRPROP) are what
	 * fills the buffer. */
	std::string ParametersPrelude(const std::vector<Parameter>& p_parameters, uint32_t p_blockSize)
	{
		std::ostringstream out;

		if (p_blockSize > 0)
		{
			out << "layout(std140, binding = 2) uniform MaterialParams\n{\n";
			for (const Parameter& parameter : p_parameters)
			{
				if (IsSamplerType(parameter.type))
				{
					continue;
				}
				out << "    " << GlslType(parameter.type) << " " << parameter.name;
				if (!parameter.count.empty())
				{
					out << "[" << parameter.count << "]";
				}
				out << ";\n";
			}
			out << "};\n";
		}

		for (const Parameter& parameter : p_parameters)
		{
			if (!IsSamplerType(parameter.type))
			{
				continue;
			}
			out << "layout(binding = " << parameter.binding << ") uniform "
				<< GlslType(parameter.type) << " " << parameter.name << ";\n";
		}

		out << "\n";
		return out.str();
	}

	/** The `#define`s one variant is compiled with: the stage, then the pass, then the
	 * features. The stage one is what lets Common/Utils.glsl keep its derivative helper out
	 * of the stages that cannot call it (see ComputeTBN there). */
	std::string VariantDefines(const PassVariant& p_variant, Stage p_stage)
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
	constexpr char kDefinesPlaceholder[] = "//@MOON_VARIANT_DEFINES@";

	/** One complete stage source: version, defines, prelude, the entry point, main.
	 *
	 * The #line directives point back into the .mat the user wrote, so a compile error names
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
	std::string BuildStage(
		Stage p_stage,
		const MaterialFile& p_material,
		const PassVariant& p_variant,
		const std::string& p_prelude,
		const std::string& p_fileName,
		const std::vector<std::string>& p_includeDirs,
		std::string& p_error,
		bool p_shared = false)
	{
		const std::string& body = p_stage == Stage::Vertex ? p_material.vertexBody
			: p_stage == Stage::Fragment ? p_material.fragmentBody
			: p_material.geometryBody;
		const uint32_t entryLine = p_stage == Stage::Vertex ? p_material.vertexEntryLine
			: p_stage == Stage::Fragment ? p_material.fragmentEntryLine
			: p_material.geometryEntryLine;
		const bool rawStage = p_stage == Stage::Geometry || DefinesMain(body);

		std::ostringstream out;
		out << "#version 450 core\n"
			// The #line directives below carry a file name, which is an extension (filament
			// enables it the same way, see its CodeGenerator.cpp). Without it glslang rejects
			// the directive outright, so the build-time validation could not read what we
			// generate; drivers accept the form either way.
			<< "#extension GL_GOOGLE_cpp_style_line_directive : enable\n"
			<< "// generated by MoonMatc from " << p_fileName << ", stage " << StageName(p_stage) << '\n';
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
		out << p_prelude;
		if (!rawStage)
		{
			out << VaryingsPrelude(p_stage == Stage::Vertex, p_material.domain);
		}
		out << ParametersPrelude(p_material.parameters, p_material.parameterBlockSize)
			<< "\n#line " << entryLine << " \"" << p_fileName << "\"\n"
			<< body
			<< "\n#line 1 \"<generated>\"\n";
		if (!rawStage)
		{
			out << (p_stage == Stage::Vertex
				? VertexSkeleton(!p_material.vertexBody.empty(), p_material.domain)
				: FragmentSkeleton());
		}

		// The material's own #includes are expanded here, into the stage that is about to be
		// compiled - there is no runtime pass left to do it once it is a package.
		std::set<std::string> visited;
		std::string expanded;
		if (!ExpandIncludes(out.str(), p_includeDirs, p_fileName, visited, expanded, p_error))
		{
			return {};
		}
		return expanded;
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
} // anonymous namespace

CompileResult CompileSource(const std::string& p_sourceText, const CompileOptions& p_options)
{
	CompileResult result;

	const std::string& inputPath = p_options.displayName;
	const std::string& outputPath = p_options.outputPath;
	const std::string& dumpGlslDir = p_options.dumpGlslDir;
	const std::string& dumpJsonPath = p_options.dumpJsonPath;
	const std::vector<std::string>& includeDirs = p_options.includeDirs;
	const bool preprocessOnly = p_options.preprocessOnly;

	const std::string& text = p_sourceText;

	MaterialFile material;
	std::string error;
	if (!ParseMaterialFile(text, material, error))
	{
		// Not a material file yet (the shaders are migrated one at a time): copy it through
		// byte for byte, which is exactly what the pipeline did before MoonMatc existed.
		result.passthrough = true;
		result.package = text;
		result.success = true;
		if (!outputPath.empty() && !WriteFile(outputPath, text))
		{
			result.success = false;
			result.error = "cannot write " + outputPath;
			return result;
		}
		return result;
	}

	// The material's descriptors: std140 offsets for the parameter block and the sampler units.
	// Done before code generation and before the package is written, so both sides use the same
	// numbers.
	LayoutParameters(material);

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
	for (const std::string& relative : { "Common/Buffers/EngineUBO.glsl", "Common/Utils.glsl" })
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

	// The shared stage texts, one per stage the material has, with the variant defines left as
	// a placeholder. Everything else in a stage is the same for every variant, so this is the
	// difference between "192 copies of the same 64 KB" and one.
	std::map<std::string, std::string> sharedStages;
	{
		const PassVariant none;
		for (const Stage stage : { Stage::Vertex, Stage::Fragment })
		{
			std::string text = BuildStage(
				stage, material, none, commonPrelude, nameForLines, includeDirs, error, true);
			if (text.empty())
			{
				result.error = error;
				return result;
			}
			sharedStages[StageName(stage)] = std::move(text);
		}
		if (!material.geometryBody.empty())
		{
			std::string text = BuildStage(
				Stage::Geometry, material, none, commonPrelude, nameForLines, includeDirs, error, true);
			if (text.empty())
			{
				result.error = error;
				return result;
			}
			sharedStages[StageName(Stage::Geometry)] = std::move(text);
		}
	}

	// Entries: "pass/features/stage" -> the variant's #define lines, and the reflection table.
	// The full source of a stage is sharedStages[stage] with those lines spliced in.
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

		blobs[prefix + "vertex"] = VariantDefines(variant, Stage::Vertex);
		blobs[prefix + "fragment"] = VariantDefines(variant, Stage::Fragment);
		blobOrder.push_back(prefix + "vertex");
		blobOrder.push_back(prefix + "fragment");

		// A geometry stage is written by the material and only exists for the shaders that
		// expand primitives, so it is emitted when the block is there and left out otherwise.
		// It is also left out for the variants whose guard feature is off: the shaders that
		// use one open it with `#if defined(WITH_EDGE)`, and the old pipeline skipped the
		// stage there rather than running a pass-through.
		if (!material.geometryBody.empty() && WantsGeometryStage(material.geometryBody, variant))
		{
			blobs[prefix + "geometry"] = VariantDefines(variant, Stage::Geometry);
			blobOrder.push_back(prefix + "geometry");
		}
	}

	// Reflection: the parameters this material exposes and the vertex attributes its stages
	// read, in the shape a panel (or a material editor) can read straight away. This is the
	// whole point of doing it offline: no glGetActiveUniform at run time, and the defaults the
	// material file declared come along with it.
	const std::vector<VertexAttribute> attributes = CollectVertexAttributes(sharedStages.at("vertex"));

	std::ostringstream json;
	json << "{\n  \"name\": \"" << material.name << "\",\n"
		<< "  \"shadingModel\": \"" << material.shadingModel << "\",\n"
		<< "  \"domain\": \"" << material.domain << "\",\n"
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
		const Parameter& parameter = material.parameters[i];
		json << "    { \"name\": \"" << parameter.name
			<< "\", \"type\": \"" << parameter.type
			<< "\", \"glslType\": \"" << GlslType(parameter.type) << "\""
			<< ", \"sampler\": " << (IsSamplerType(parameter.type) ? "true" : "false");
		if (!parameter.count.empty())
		{
			json << ", \"count\": " << parameter.count;
		}
		if (!parameter.defaultValue.empty())
		{
			json << ", \"default\": \"" << parameter.defaultValue << "\"";
		}
		if (!parameter.label.empty())
		{
			json << ", \"label\": \"" << JsonEscape(parameter.label) << "\"";
		}
		if (!parameter.group.empty())
		{
			json << ", \"group\": \"" << JsonEscape(parameter.group) << "\"";
		}
		if (parameter.hasRange)
		{
			json << ", \"min\": " << parameter.min << ", \"max\": " << parameter.max;
		}
		if (parameter.hasStep)
		{
			json << ", \"step\": " << parameter.step;
		}
		if (parameter.color)
		{
			json << ", \"color\": true";
		}
		if (!parameter.ui)
		{
			json << ", \"ui\": false";
		}
		if (IsSamplerType(parameter.type))
		{
			json << ", \"binding\": " << parameter.binding;
		}
		else
		{
			json << ", \"offset\": " << parameter.offset << ", \"stride\": " << parameter.stride;
		}
		json << " }" << (i + 1 < material.parameters.size() ? "," : "") << "\n";
	}
	json << "  ],\n  \"parameterBlockSize\": " << material.parameterBlockSize << ",\n  \"attributes\": [\n";
	for (size_t i = 0; i < attributes.size(); ++i)
	{
		json << "    { \"location\": " << attributes[i].location
			<< ", \"name\": \"" << attributes[i].name
			<< "\", \"glslType\": \"" << attributes[i].glslType << "\" }"
			<< (i + 1 < attributes.size() ? "," : "") << "\n";
	}
	json << "  ]\n}\n";

	result.json = json.str();
	if (!dumpJsonPath.empty() && !WriteFile(dumpJsonPath, result.json))
	{
		result.error = "cannot write " + dumpJsonPath;
		return result;
	}

	// The full source of a stage: its shared text with this variant's defines spliced in.
	const auto composeStage = [&sharedStages](const std::string& p_stage, const std::string& p_defines) {
		std::string text = sharedStages.at(p_stage);
		const size_t at = text.find(kDefinesPlaceholder);
		if (at != std::string::npos)
		{
			text.replace(at, sizeof(kDefinesPlaceholder) - 1, p_defines);
		}
		return text;
	};

	if (!dumpGlslDir.empty())
	{
		for (const std::string& key : blobOrder)
		{
			std::string fileNameOut = key;
			std::replace(fileNameOut.begin(), fileNameOut.end(), '/', '_');
			std::replace(fileNameOut.begin(), fileNameOut.end(), '|', '-');

			// The last path component is the stage name the shared text is keyed by.
			const std::string stage = key.substr(key.rfind('/') + 1);
			if (!WriteFile(std::filesystem::path(dumpGlslDir) / (fileNameOut + ".glsl"),
					composeStage(stage, blobs[key])))
			{
				result.error = "cannot dump " + fileNameOut + ".glsl into " + dumpGlslDir;
				return result;
			}
		}
	}

	if (preprocessOnly)
	{
		const std::string& first = blobOrder.front();
		result.preprocessed = composeStage(first.substr(first.rfind('/') + 1), blobs[first]);
		result.materialName = material.name;
		result.variantCount = static_cast<uint32_t>(variants.size());
		result.success = true;
		return result;
	}

	// The package itself.
	std::string package;
	package.append(kMagic, sizeof(kMagic));

	// SHDRSTAG: the shared stage texts (one per stage), each with kDefinesPlaceholder where the
	// variant's #define lines belong. This is what keeps a 192-variant material from carrying
	// 192 copies of the same source.
	{
		std::string payload;
		const uint32_t count = static_cast<uint32_t>(sharedStages.size());
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const auto& [stage, text] : sharedStages)
		{
			const uint32_t nameSize = static_cast<uint32_t>(stage.size());
			const uint32_t size = static_cast<uint32_t>(text.size());
			payload.append(reinterpret_cast<const char*>(&nameSize), sizeof(nameSize));
			payload.append(stage);
			payload.append(reinterpret_cast<const char*>(&size), sizeof(size));
			payload.append(text);
		}
		AppendChunk(package, "SHDRSTAG", payload);
	}

	// SHDRGLSL: the per-variant defines - a count, then (key length, key, size, bytes) per entry.
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

	// SHDRPARM: name/type pairs of the material parameters (the binary form of the JSON). Kept as
	// it was for the first readers; SHDRPROP below is the full reflection.
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

	// SHDRPROP: the material's properties as a panel needs them - name, the DSL type it was
	// declared with, the GLSL type it becomes, whether it is a sampler (texture slot) or a plain
	// uniform, its array size, the GLSL initialiser it declared, and the optional designer fields
	// (label, group, range, step, colour, hidden).
	//
	// filament splits this into a UIB (uniform interface block: name, size, type, precision) and
	// a SIB (sampler interface block: name, binding, type, format); we keep plain uniforms and
	// bind textures by name, so there is nothing to lay out and no descriptor set to index - one
	// list with a flag carries the same information.
	{
		std::string payload;
		const uint32_t count = static_cast<uint32_t>(material.parameters.size());
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const Parameter& parameter : material.parameters)
		{
			const std::string glslType = GlslType(parameter.type);
			const uint32_t arrayCount = parameter.count.empty()
				? 0u : static_cast<uint32_t>(std::stoul(parameter.count));
			const uint8_t sampler = IsSamplerType(parameter.type) ? 1 : 0;
			const uint32_t nameSize = static_cast<uint32_t>(parameter.name.size());
			const uint32_t dslSize = static_cast<uint32_t>(parameter.type.size());
			const uint32_t glslSize = static_cast<uint32_t>(glslType.size());
			const uint32_t defaultSize = static_cast<uint32_t>(parameter.defaultValue.size());
			const uint32_t labelSize = static_cast<uint32_t>(parameter.label.size());
			const uint32_t groupSize = static_cast<uint32_t>(parameter.group.size());
			const uint8_t flags =
				(parameter.hasRange ? 0x01 : 0x00) |
				(parameter.hasStep ? 0x02 : 0x00) |
				(parameter.color ? 0x04 : 0x00) |
				(parameter.ui ? 0x00 : 0x08);

			payload.append(reinterpret_cast<const char*>(&nameSize), sizeof(nameSize));
			payload.append(parameter.name);
			payload.append(reinterpret_cast<const char*>(&dslSize), sizeof(dslSize));
			payload.append(parameter.type);
			payload.append(reinterpret_cast<const char*>(&glslSize), sizeof(glslSize));
			payload.append(glslType);
			payload.append(reinterpret_cast<const char*>(&arrayCount), sizeof(arrayCount));
			payload.append(reinterpret_cast<const char*>(&sampler), sizeof(sampler));
			payload.append(reinterpret_cast<const char*>(&defaultSize), sizeof(defaultSize));
			payload.append(parameter.defaultValue);
			payload.append(reinterpret_cast<const char*>(&labelSize), sizeof(labelSize));
			payload.append(parameter.label);
			payload.append(reinterpret_cast<const char*>(&groupSize), sizeof(groupSize));
			payload.append(parameter.group);
			payload.append(reinterpret_cast<const char*>(&parameter.min), sizeof(parameter.min));
			payload.append(reinterpret_cast<const char*>(&parameter.max), sizeof(parameter.max));
			payload.append(reinterpret_cast<const char*>(&parameter.step), sizeof(parameter.step));
			payload.append(reinterpret_cast<const char*>(&flags), sizeof(flags));

			// The descriptor layout: std140 offset/stride for the parameter block, the texture
			// unit for a sampler.
			payload.append(reinterpret_cast<const char*>(&parameter.offset), sizeof(parameter.offset));
			payload.append(reinterpret_cast<const char*>(&parameter.stride), sizeof(parameter.stride));
			payload.append(reinterpret_cast<const char*>(&parameter.binding), sizeof(parameter.binding));
		}
		payload.append(reinterpret_cast<const char*>(&material.parameterBlockSize), sizeof(material.parameterBlockSize));
		AppendChunk(package, "SHDRPROP", payload);
	}

	// SHDRATTR: the vertex attributes the material reads - location, GLSL type, name. Same idea
	// as filament's MaterialAttributeInfo chunk, so a VAO can be checked against a material
	// without compiling it.
	{
		std::string payload;
		const uint32_t count = static_cast<uint32_t>(attributes.size());
		payload.append(reinterpret_cast<const char*>(&count), sizeof(count));
		for (const VertexAttribute& attribute : attributes)
		{
			const uint32_t nameSize = static_cast<uint32_t>(attribute.name.size());
			const uint32_t typeSize = static_cast<uint32_t>(attribute.glslType.size());
			payload.append(reinterpret_cast<const char*>(&attribute.location), sizeof(attribute.location));
			payload.append(reinterpret_cast<const char*>(&typeSize), sizeof(typeSize));
			payload.append(attribute.glslType);
			payload.append(reinterpret_cast<const char*>(&nameSize), sizeof(nameSize));
			payload.append(attribute.name);
		}
		AppendChunk(package, "SHDRATTR", payload);
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

	result.package = std::move(package);
	result.materialName = material.name;
	result.variantCount = static_cast<uint32_t>(variants.size());
	result.success = true;

	if (!outputPath.empty() && !WriteFile(outputPath, result.package))
	{
		result.success = false;
		result.error = "cannot write " + outputPath;
	}
	return result;
}

CompileResult CompileFile(const std::string& p_path, CompileOptions p_options)
{
	if (p_options.displayName.empty())
	{
		p_options.displayName = std::filesystem::path(p_path).filename().string();
	}

	bool ok = false;
	const std::string text = ReadFile(p_path, ok);
	if (!ok)
	{
		CompileResult result;
		result.error = "cannot read " + p_path;
		return result;
	}
	return CompileSource(text, p_options);
}

} // namespace Moon::Matc
