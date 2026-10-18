#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <iostream>
#include <unordered_set>
#include<assert.h>
#include <glad/glad.h>
#include <Rendering/HAL/ShaderProgram.h>
#include <Rendering/HAL/ShaderStage.h>
#include <Rendering/Resources/Loaders/ShaderLoader.h>
#include <Rendering/Resources/Shader.h>
#include <Rendering/Resources/ShaderArchive.h>
#include <Rendering/Resources/ShaderPackage.h>
#include <Rendering/Utils/ShaderUtil.h>

namespace
{
	namespace Grammar
	{
		constexpr std::string_view kPassToken = "#pass";
		constexpr std::string_view kFeatureToken = "#feature";
		constexpr std::string_view kVertexShaderToken = "#shader vertex";
		constexpr std::string_view kFragmentShaderToken = "#shader fragment";
		constexpr std::string_view kGeomertyShaderToken = "#shader geomerty";
		constexpr std::string_view kIncludeToken = "#include";
		constexpr std::string_view kDefineToken = "#define";
		constexpr std::string_view kVersionToken = "#version";
	}

	struct ShaderInputInfo
	{
		std::string path;
		std::string name;
	};

	Rendering::Resources::Loaders::ShaderLoader::LoggingSettings __LOGGING_SETTINGS{
		.summary = false,
		.linkingErrors = true,
		.linkingSuccess = false,
		.compilationErrors = true,
		.compilationSuccess = false,
	};

	/** Where one line of the assembled shader source came from.
	 *
	 * The loader splices included files in line by line and the parser then cuts the
	 * file into one source per shader stage, so the line a driver complains about has
	 * nothing to do with the line the user wrote. Every assembled line keeps its origin
	 * here, and the stage sources are annotated with #line directives taken from it, so
	 * a compile error points at the file and line it really came from. */
	struct SourceOrigin
	{
		std::string file;
		uint32_t line = 0; // 1-based, as an editor counts them
	};

	struct ShaderLoadResult
	{
		const ShaderInputInfo inputInfo;
		const std::string source;
		/** One entry per line of `source`, in the same order. */
		const std::vector<SourceOrigin> origins;
	};

	struct ShaderParseResult
	{
		const ShaderInputInfo inputInfo;
		const std::string vertexShader;
		const std::string fragmentShader;
		const std::string geomertyShader;
		const std::unordered_set<std::string> passes;
		const Rendering::Data::FeatureSet features;
		/** Every file the source was assembled from: the shader itself and everything
		 * it includes. The hot reload watches these, not just the shader. */
		const std::unordered_set<std::string> sourceFiles;
	};

	struct ShaderAssembleResult
	{
		const ShaderInputInfo inputInfo;
		const uint32_t failures; // How many variants failed to compile
		Rendering::Resources::Shader::Variants variants;
	};

	struct ShaderStageDesc
	{
		const std::string source;
		const Rendering::Settings::EShaderType type;
	};

	struct ProcessedShaderStage
	{
		const Rendering::HAL::ShaderStage stage;
		std::optional<Rendering::Settings::ShaderCompilationResult> compilationResult;
	};

	std::string Trim(const std::string_view p_str)
	{
		auto view =
			p_str |
			std::views::drop_while(isspace) |
			std::views::reverse |
			std::views::drop_while(isspace) |
			std::views::reverse;

		return std::string{ view.begin(), view.end() };
	}

	std::string FeatureSetToString(const Rendering::Data::FeatureSet& features)
	{
		if (!features.empty())
		{
			return std::format("({})", std::accumulate(
				std::next(features.begin()), features.end(), *features.begin(),
				[](const auto& a, const auto& b) {
					return std::format("{}|{}", a, b);
				}
			));
		}

		return std::string{};
	}

	std::string AddDefinesToShaderCode(
		const std::string& shaderCode,
		const std::unordered_set<std::string_view> defines
	)
	{
		if (defines.empty())
		{
			return shaderCode;
		}

		// Create the defines string once
		std::string definesStr;
		for (const auto& define : defines)
		{
			if (!define.empty())
			{
				definesStr += std::format("{} {}\n", Grammar::kDefineToken, define);
			}
		}

		// Find insertion point after version directive if it exists
		const size_t versionPos = shaderCode.find(Grammar::kVersionToken);
		if (versionPos == std::string::npos)
		{
			// No version directive, prepend defines
			return definesStr + shaderCode;
		}

		// Find end of version line
		const size_t endOfLine = shaderCode.find('\n', versionPos);
		if (endOfLine == std::string::npos)
		{
			// Version directive is on the last line (unusual case)
			return shaderCode + "\n" + definesStr;
		}

		// Insert defines after version line
		std::string result = shaderCode;
		result.insert(endOfLine + 1, definesStr);
		return result;
	}

	std::unique_ptr<Rendering::HAL::ShaderProgram> CreateProgram(
		const ShaderInputInfo& p_shaderInputInfo,
		std::span<const ShaderStageDesc> p_stages,
		const std::string_view p_pass,
		const Rendering::Data::FeatureSet& p_features,
		bool p_disableLogging = false
	)
	{
		// Process and compile all shader stages
		std::vector<ProcessedShaderStage> processedStages;
		processedStages.reserve(p_stages.size());

		bool compilationFailed = false;

		for (const auto& stageInput : p_stages)
		{
			if (stageInput.type == Rendering::Settings::EShaderType::GEOMERTY) {
				int i = 0;
				bool startWithIf = false;
				std::string define = "";
				// Find insertion point after version directive if it exists
				const size_t versionPos = stageInput.source.find(Grammar::kVersionToken);
	
				// Find end of version line
				const size_t endOfLine = stageInput.source.find('\n', versionPos);
				if (endOfLine != std::string::npos)
				{
					// Version directive is on the last line (unusual case)
					i = endOfLine+1;
				}
				// Step over the directives that carry no code before the guard this scan is
				// after: the loader puts a #line anchor right behind #version, and the whole
				// stage is usually wrapped in the #if defined(...) that follows it. Without
				// this the scan saw the anchor, decided the stage had no guard, and compiled
				// an empty geometry shader ("No input primitive type").
				for (;;)
				{
					while (i < static_cast<int>(stageInput.source.size())
						&& (stageInput.source[i] == ' ' || stageInput.source[i] == '\t'
							|| stageInput.source[i] == '\n' || stageInput.source[i] == '\r'))
					{
						++i;
					}
					const bool isLineDirective = stageInput.source.compare(i, 5, "#line") == 0;
					const bool isComment = stageInput.source.compare(i, 2, "//") == 0;
					if (!isLineDirective && !isComment)
					{
						break;
					}
					const size_t eol = stageInput.source.find('\n', i);
					i = (eol == std::string::npos) ? static_cast<int>(stageInput.source.size())
						: static_cast<int>(eol) + 1;
				}
				while (i<stageInput.source.size())
				{
					char ch = stageInput.source[i];
					if (ch == ' ' || ch == '\n' || ch == '\r') {
						i++;
					}
					else
					{
						if (ch == '#') {
							if (i + 2 < stageInput.source.size()) {
								if (stageInput.source[i + 1] == 'i' && stageInput.source[i + 2] == 'f') {
									startWithIf = true;
									int offset=stageInput.source.find("defined");
									int a=stageInput.source.find('(',offset );
									int b= stageInput.source.find( ')',offset);
									while (stageInput.source[a]==' ' || stageInput.source[a] == '(')
									{
										a++;
									}
									while (a<b)
									{
										if (stageInput.source[a] != ' ') {
											define += stageInput.source[a];
											a++;
										}
										else
										{
											break;
										}
									}
									break;
								}
								else
								{
									break;
								}

							}
							break;
						}
						else
						{
							break;
						}
					}
				}
				
				if (startWithIf&& !p_features.contains(define)) {
					continue;
			    }
			}
			const auto& processedStage = processedStages.emplace_back(stageInput.type);

			std::unordered_set<std::string_view> defines(p_features.begin(), p_features.end());
			defines.insert(p_pass);
			const auto source =  AddDefinesToShaderCode(stageInput.source, defines);
			processedStage.stage.Upload(source);

			if (const auto result = processedStage.stage.Compile(); !result.success)
			{
				if (!p_disableLogging && __LOGGING_SETTINGS.compilationErrors)
				{
					std::cout << (std::format(
						"[Shader Compile] {}<{}>{}: {}",
						std::format("{}/{}",
							p_shaderInputInfo.name,
							Rendering::Utils::GetShaderTypeName(stageInput.type)
						),
						p_pass,
						FeatureSetToString(p_features),
						result.message
					))<<std::endl;;
				}

				compilationFailed = true;
			}
			else if (!p_disableLogging && __LOGGING_SETTINGS.compilationSuccess)
			{
				std::cout <<
					std::format(
						"[Shader Compile] {}<{}>{}: Compilation successful.",
						std::format("{}/{}",
							p_shaderInputInfo.name,
							Rendering::Utils::GetShaderTypeName(stageInput.type)
						),
						p_pass,
						FeatureSetToString(p_features)
					) << std::endl;
		
			}
		}

		if (compilationFailed)
		{
			return nullptr;
		}

		// Link the program
		auto program = std::make_unique<Rendering::HAL::ShaderProgram>();

		// Attach all stages
		for (const auto& processedStage : processedStages)
		{
			program->Attach(processedStage.stage);
		}

		// Link and detach regardless of result
		const auto linkResult = program->Link();

		// Detach all stages
		for (const auto& processedStage : processedStages)
		{
			program->Detach(processedStage.stage);
		}

		if (linkResult.success)
		{
			if (!p_disableLogging && __LOGGING_SETTINGS.linkingSuccess)
			{
				std::cout << (std::format(
					"[Shader Linking] {}<{}>{}: {}",
					p_shaderInputInfo.name,
					p_pass,
					FeatureSetToString(p_features),
					"Linking successful."
				)) << std::endl;;
			}

			return program;
		}
		else
		{
			if (!p_disableLogging && __LOGGING_SETTINGS.linkingErrors)
			{
				std::cout << (std::format(
					"[Shader Linking] {}{}: {}",
					p_shaderInputInfo.name,
					FeatureSetToString(p_features),
					linkResult.message
				)) << std::endl;;
			}

			return nullptr;
		}
	}

	std::unique_ptr<Rendering::HAL::ShaderProgram> CreateDefaultProgram()
	{
		const std::string vertex = R"(
#version 450 core

layout(location = 0) in vec3 geo_Pos;

void main()
{
	gl_Position = vec4(geo_Pos, 1.0);
}
)";

		const std::string fragment = R"(
#version 450 core

out vec4 FragColor;

void main()
{
	FragColor = vec4(1.0, 0.0, 1.0, 1.0);
}
)";

		auto shaders = std::array<ShaderStageDesc, 2>{
			ShaderStageDesc{ vertex, Rendering::Settings::EShaderType::VERTEX },
				ShaderStageDesc{ fragment, Rendering::Settings::EShaderType::FRAGMENT }
		};

		auto program = CreateProgram(
			ShaderInputInfo{
				.path = "N/A",
				.name = "DefaultProgram"
			},
			shaders,
			{},
			{},
			true // Force no logging for default program (we expect it to succeed, otherwise we have a problem)
		);

		assert(program != nullptr&&"Failed to create default shader program");
		return std::move(program);
	}

	std::optional<std::string> ParseIncludeDirective(const std::string_view line)
	{
		const auto start = line.find('"');
		const auto end = line.find('"', start + 1);

		if (start != std::string_view::npos && end != std::string_view::npos && end > start)
		{
			return std::make_optional<std::string>(line.substr(start + 1, end - start - 1));
		}

		return std::nullopt;
	}
	/** Splits one source into lines and appends it to the assembled source, recording
	 * where every appended line came from. Used for the shader itself and, through the
	 * recursion below, for every file it includes. */
	ShaderLoadResult LoadShader(
		const ShaderInputInfo& p_shaderInputInfo,
		const std::string& p_filePath,
		Rendering::Resources::Loaders::ShaderLoader::FilePathParserCallback p_pathParser
	);

	ShaderLoadResult AssembleSource(
		const ShaderInputInfo& p_shaderInputInfo,
		const std::string& p_text,
		const std::string& p_fileForOrigins,
		Rendering::Resources::Loaders::ShaderLoader::FilePathParserCallback p_pathParser
	)
	{
		std::stringstream buffer;
		std::vector<SourceOrigin> origins;

		std::istringstream iss(p_text);
		std::string line;
		uint32_t lineNumber = 1;

		while (std::getline(iss, line))
		{
			const std::string trimmedLine = Trim(line);

			if (trimmedLine.starts_with(Grammar::kIncludeToken))
			{
				// If the line contains #include, process the included file
				if (const auto includeFilePath = ParseIncludeDirective(line))
				{
					// Recursively load the included file
					const std::string realIncludeFilePath = p_pathParser ? p_pathParser(includeFilePath.value()) : includeFilePath.value();
					const auto included = LoadShader(p_shaderInputInfo, realIncludeFilePath, p_pathParser);
					// The included lines keep the file and line they were written on: the
					// parser turns that into #line directives, so an error inside the
					// .ovfxh names the .ovfxh instead of the concatenation it ended up in.
					buffer << included.source;
					origins.insert(origins.end(), included.origins.begin(), included.origins.end());
				}
				else
				{
					std::cout << (std::format("[Shader Loading] Invalid #include directive in file: \"{}\"", p_fileForOrigins)) << std::endl;
				}
			}
			else
			{
				// If the line does not contain #include, just append it to the buffer
				buffer << line << '\n';
				origins.push_back({ p_fileForOrigins, lineNumber });
			}

			++lineNumber;
		}

		return {
			.inputInfo = p_shaderInputInfo,
			.source = buffer.str(),
			.origins = std::move(origins)
		};
	}

	ShaderLoadResult LoadShader(
		const ShaderInputInfo& p_shaderInputInfo,
		const std::string& p_filePath,
		Rendering::Resources::Loaders::ShaderLoader::FilePathParserCallback p_pathParser
	)
	{
		// The source comes out of the archive the build packed it into; the disk is only
		// the fallback for a checkout that has not been packed yet (see ShaderArchive.h).
		std::string text;
		std::string origin;
		if (!Rendering::Resources::ReadShaderSource(p_filePath, text, &origin))
		{
			std::cout << (std::format("[Shader Loading] Could not open shader file: \"{}\"", p_filePath)) << std::endl;
			return {};
		}

		return AssembleSource(p_shaderInputInfo, text, origin, p_pathParser);
	}

	/** The same for a source the caller already holds (the path tracing pass builds a
	 * couple of them by editing the text of a file). There is no file to name, so the
	 * lines are attributed to the shader's own name. */
	ShaderLoadResult LoadShader(
		const std::string& p_source,
		Rendering::Resources::Loaders::ShaderLoader::FilePathParserCallback p_pathParser
	)
	{
		return AssembleSource(ShaderInputInfo{}, p_source, std::string{ "{source}" }, p_pathParser);
	}

	/**
	* Parse the laoded shader code and extract the vertex and fragment shaders, as well as features
	*/
	ShaderParseResult ParseShader(const ShaderLoadResult& p_shaderLoadResult)
	{
		using namespace Rendering::Settings;

		std::istringstream stream(p_shaderLoadResult.source); // Add this line to create a stringstream from shaderCode
		std::string line;
		std::unordered_map<EShaderType, std::stringstream> shaderSources;
		Rendering::Data::FeatureSet features;
		std::unordered_set<std::string> passes;
		passes.emplace(); // Default pass if none is specified (empty string)
		std::unordered_set<std::string> sourceFiles;

		auto currentType = EShaderType::NONE;

		// The line numbers a driver reports are the numbers of the source it was handed -
		// the stage alone, with the file's headers cut away and the includes spliced in.
		// So every stage starts with a #line that puts the counter back where the text
		// really lives, and one is emitted again wherever the origin jumps (the start of
		// an included file, and its end).
		//
		// It can only go in after the stage's #version, though: GLSL wants that to be the
		// first statement, so a #line in front of it is rejected outright. Stages that start
		// with a blank line or a comment before their #version are why this waits for the
		// directive instead of taking the first line it sees.
		bool anchorPending = false;
		bool versionPending = false;
		SourceOrigin lastOrigin{};

		size_t lineIndex = 0;
		while (std::getline(stream, line))
		{
			// Falls back to the assembled numbering if the origins and the text ever
			// disagree, which would be a bug in this file and not worth failing a load
			// over.
			const SourceOrigin origin = lineIndex < p_shaderLoadResult.origins.size()
				? p_shaderLoadResult.origins[lineIndex]
				: SourceOrigin{ p_shaderLoadResult.inputInfo.path, static_cast<uint32_t>(lineIndex + 1) };
			++lineIndex;

			const std::string trimmedLine = Trim(line);

			if (trimmedLine.starts_with(Grammar::kPassToken))
			{
				std::string passName;
				passName.reserve(16); // Reserve some arbitrary space for the pass name

				for (auto& c : trimmedLine |
					std::views::drop(Grammar::kPassToken.size()) |
					std::views::drop_while(isspace) |
					std::views::take_while([](char c) { return !isspace(c); }))
				{
					passName += c;
				}

				passes.insert(passName);
			}
			else if (trimmedLine.starts_with(Grammar::kFeatureToken))
			{
				std::string featureName;
				featureName.reserve(16); // Reserve some arbitrary space for the feature name

				for (auto& c : trimmedLine |
					std::views::drop(Grammar::kFeatureToken.size()) |
					std::views::drop_while(isspace) |
					std::views::take_while([](char c) { return !isspace(c); }))
				{
					featureName += c;
				}

				features.insert(featureName);
			}
			else if (trimmedLine.starts_with(Grammar::kVertexShaderToken))
			{
				currentType = EShaderType::VERTEX;
				anchorPending = true;
				versionPending = true;
			}
			else if (trimmedLine.starts_with(Grammar::kFragmentShaderToken))
			{
				currentType = EShaderType::FRAGMENT;
				anchorPending = true;
				versionPending = true;
			}
			else if (trimmedLine.starts_with(Grammar::kGeomertyShaderToken)) {
				currentType = EShaderType::GEOMERTY;
				anchorPending = true;
				versionPending = true;
			}
			else if (currentType != EShaderType::NONE)
			{
				auto& source = shaderSources[currentType];

				const bool isVersionLine = trimmedLine.starts_with(Grammar::kVersionToken);
				// #version has to be the first thing in the stage, so nothing is emitted in
				// front of it - the anchor for a stage begins on the line after it. Everything
				// else gets one when the origin is not simply the next line of the same file.
				const bool jumps = origin.file != lastOrigin.file
					|| origin.line != lastOrigin.line + 1;
				if (!isVersionLine && !versionPending && (anchorPending || jumps))
				{
					source << "#line " << origin.line << " \"" << origin.file << "\"\n";
					anchorPending = false;
				}

				source << line << '\n';
				if (isVersionLine)
				{
					// The next line is where the counter has to be anchored.
					versionPending = false;
					anchorPending = true;
				}

				lastOrigin = origin;
				sourceFiles.insert(origin.file);
			}
		}

		return ShaderParseResult{
			.inputInfo = p_shaderLoadResult.inputInfo,
			.vertexShader = shaderSources[EShaderType::VERTEX].str(),
			.fragmentShader = shaderSources[EShaderType::FRAGMENT].str(),
			.geomertyShader = shaderSources[EShaderType::GEOMERTY].str(),
			.passes = passes,
			.features = features,
			.sourceFiles = std::move(sourceFiles)
		};
	}

	/**
	* Compile and create programs for each shader variant, and assemble them for a shader to use.
	*/
	ShaderAssembleResult AssembleShader(const ShaderParseResult& p_parseResult)
	{
		const auto startTime = std::chrono::high_resolution_clock::now();

		const auto featureVariantCount = (size_t{ 1UL } << p_parseResult.features.size());

		uint32_t failures = 0;

		Rendering::Resources::Shader::Variants variants;
		auto stages = std::vector<ShaderStageDesc>(
			{
			  { p_parseResult.vertexShader, Rendering::Settings::EShaderType::VERTEX },
			  { p_parseResult.fragmentShader, Rendering::Settings::EShaderType::FRAGMENT }
			});
		if (p_parseResult.geomertyShader != "") {
			stages.push_back({ p_parseResult.geomertyShader, Rendering::Settings::EShaderType::GEOMERTY });
		}
		// We create as many additional shader programs (variants) as there are passes
		for (const auto& pass : p_parseResult.passes)
		{
			Rendering::Resources::Shader::FeatureVariants featureVariants;
			featureVariants.reserve(featureVariantCount);

			// We create a shader program (AKA shader variant) for each combination of features.
			// The number of combinations is 2^n, where n is the number of features.
			for (size_t i = 0; i < featureVariantCount; ++i)
			{
				Rendering::Data::FeatureSet featureSet;
				for (size_t j = 0; j < p_parseResult.features.size(); ++j)
				{
					if (i & (size_t{ 1UL } << j))
					{
						featureSet.insert(*std::next(p_parseResult.features.begin(), j));
					}
				}
				auto program = CreateProgram(
					p_parseResult.inputInfo,
					stages,
					pass,
					featureSet
				);

				if (program)
				{
					featureVariants.emplace(featureSet, std::move(program));
				}
				else
				{
					++failures;
				}
			}

			// If no default program was created, we create a default one (fallback)
			if (!featureVariants.contains({}))
			{
				featureVariants.emplace(
					Rendering::Data::FeatureSet{},
					std::move(CreateDefaultProgram())
				);
			}

			variants.emplace(pass, std::move(featureVariants));
		}

		const auto endTime = std::chrono::high_resolution_clock::now();

		const size_t totalVariantCount = std::accumulate(
			variants.begin(), variants.end(), 0ULL,
			[](const auto& a, const auto& b) {
				return a + b.second.size();
			}
		);

		if (failures > 0)
		{
			if (__LOGGING_SETTINGS.summary)
			{
				std::cout << (std::format(
					"[Shader Assembling] {}: {} variant(s) failed to compile.",
					p_parseResult.inputInfo.name,
					failures
				)) << std::endl;
			}
		}
		else if (__LOGGING_SETTINGS.summary)
		{
			std::cout << (std::format(
				"[Shader Assembling] {}: {} variant(s) assembled in {} ms.",
				p_parseResult.inputInfo.name,
				totalVariantCount,
				std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count()
			)) << std::endl;
		}

		return ShaderAssembleResult{
			p_parseResult.inputInfo,
			failures,
			std::move(variants)
		};
	}

	ShaderAssembleResult CompileShaderFromFile(
		const std::string& p_filePath,
		Rendering::Resources::Loaders::ShaderLoader::FilePathParserCallback p_pathParser
	)
	{
		const auto shaderInputInfo = ShaderInputInfo{
			.path = p_filePath,
			.name = std::filesystem::path{ p_filePath }.stem().string()
		};

		const auto shaderLoadResult = LoadShader(shaderInputInfo, p_filePath, p_pathParser);
		const auto shaderParseResult = ParseShader(shaderLoadResult);
		return AssembleShader(shaderParseResult);
	}

	ShaderAssembleResult CompileShaderFromSources(
		const std::string& p_vertexShader,
		const std::string& p_fragmentShader,
		const std::string& p_geomertyShader
	)
	{
		const ShaderParseResult shaderParseResult{
			.inputInfo = {
				.path = "N/A",
				.name = "{C++ embedded shader}"
			},
			.vertexShader = p_vertexShader,
			.fragmentShader = p_fragmentShader,
			.geomertyShader = p_geomertyShader,
			.passes = {{}}, // Default pass (empty string)
			.features = {} // No support for features in embedded shaders
		};

		return AssembleShader(shaderParseResult);
	}

	/** Checks the driver's std140 layout against the package's, once per shader.
	 *
	 * The generated GLSL declares `layout(std140, binding = 2) uniform MaterialParams` and the
	 * runtime writes that block by the package's offsets, so the two have to agree. A mismatch
	 * would render every material with shifted values; reporting it here turns that into one
	 * line in the log. Members a particular variant does not read are dropped by the linker and
	 * are simply skipped. */
	void ValidateParameterBlock(
		const Rendering::Resources::ShaderPackage& p_package,
		Rendering::HAL::ShaderProgram& p_program)
	{
		if (p_package.ParameterBlockSize() == 0)
		{
			return;
		}

		const GLuint program = static_cast<GLuint>(p_program.GetID());
		const GLuint block = glGetUniformBlockIndex(program, "MaterialParams");
		if (block == GL_INVALID_INDEX)
		{
			std::cout << std::format("[Shader Layout] {}: MaterialParams is not an active block",
				p_package.Name()) << std::endl;
			return;
		}

		GLint binding = -1;
		glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_BINDING, &binding);
		if (binding != 2)
		{
			std::cout << std::format("[Shader Layout] {}: MaterialParams is bound to {}, expected 2",
				p_package.Name(), binding) << std::endl;
		}

		GLint blockSize = -1;
		glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if (blockSize > static_cast<GLint>(p_package.ParameterBlockSize()))
		{
			std::cout << std::format("[Shader Layout] {}: the driver's block is {} bytes, the package says {}",
				p_package.Name(), blockSize, p_package.ParameterBlockSize()) << std::endl;
		}

		for (const Rendering::Resources::ShaderPackage::Parameter& parameter : p_package.Parameters())
		{
			if (parameter.isSampler || parameter.offset < 0)
			{
				continue;
			}

			std::string name = parameter.name;
			if (parameter.count > 0)
			{
				name += "[0]";
			}

			const char* names[] = { name.c_str() };
			GLuint index = GL_INVALID_INDEX;
			glGetUniformIndices(program, 1, names, &index);
			if (index == GL_INVALID_INDEX)
			{
				continue; // not read by this variant
			}

			GLint offset = -1;
			glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_OFFSET, &offset);
			if (offset != static_cast<GLint>(parameter.offset))
			{
				std::cout << std::format("[Shader Layout] {}: {} is at offset {}, the package says {}",
					p_package.Name(), parameter.name, offset, parameter.offset) << std::endl;
			}
		}
	}

	/** Compiles the variants a shader package lists (MoonMatc's output).
	 *
	 * The package already holds the final GLSL of every (pass, variant, stage), so there is
	 * nothing to parse and no macro to inject: this is the "read the chunks and hand them to
	 * the driver" half of the pipeline, the one filament's MaterialParser does.
	 *
	 * p_error, when given, collects what the driver said. The editor's hot reload needs that
	 * text: a material being edited is compiled into a package and loaded here, and the failure
	 * has to be shown - with the .ovfx line the #line directives point at. */
	Rendering::Resources::Shader::Variants AssembleFromPackage(
		const Rendering::Resources::ShaderPackage& p_package,
		std::string* p_error = nullptr)
	{
		using namespace Rendering::Settings;

		Rendering::Resources::Shader::Variants variants;
		bool blockValidated = false;

		const auto report = [p_error](const std::string& p_text) {
			std::cout << p_text << std::endl;
			if (p_error != nullptr)
			{
				if (!p_error->empty()) { *p_error += "\n"; }
				*p_error += p_text;
			}
		};

		for (const Rendering::Resources::ShaderPackage::Variant& variant : p_package.Variants())
		{
			std::vector<std::unique_ptr<Rendering::HAL::ShaderStage>> stages;
			bool ok = true;

			struct StageSpec
			{
				Rendering::Resources::ShaderPackage::Stage stage;
				EShaderType type;
				const char* name;
				bool optional;   // a geometry stage only exists for the shaders that expand primitives
			};
			static constexpr StageSpec kStages[] = {
				{ Rendering::Resources::ShaderPackage::Stage::Vertex,   EShaderType::VERTEX,   "vertex",   false },
				{ Rendering::Resources::ShaderPackage::Stage::Fragment, EShaderType::FRAGMENT, "fragment", false },
				{ Rendering::Resources::ShaderPackage::Stage::Geometry, EShaderType::GEOMERTY, "geometry", true },
			};

			for (const StageSpec& spec : kStages)
			{
				std::string source = p_package.Source(variant, spec.stage);
				if (source.empty())
				{
					if (spec.optional)
					{
						continue;
					}
					report(std::format("[Shader Package] {}<{}> (pass '{}'): no source",
						p_package.Name(), spec.name, variant.pass));
					ok = false;
					break;
				}

				auto stage = std::make_unique<Rendering::HAL::ShaderStage>(spec.type);
				stage->Upload(std::move(source));
				if (const auto result = stage->Compile(); !result.success)
				{
					report(std::format("[Shader Compile] {}<{}> (pass '{}'): {}",
						p_package.Name(), spec.name, variant.pass, result.message));
					ok = false;
					break;
				}
				stages.push_back(std::move(stage));
			}

			if (!ok)
			{
				continue;
			}

			auto program = std::make_unique<Rendering::HAL::ShaderProgram>();
			for (const auto& stage : stages)
			{
				program->Attach(*stage);
			}
			const auto linkResult = program->Link();
			for (const auto& stage : stages)
			{
				program->Detach(*stage);
			}

			if (!linkResult.success)
			{
				report(std::format("[Shader Linking] {} (pass '{}'): {}",
					p_package.Name(), variant.pass, linkResult.message));
				continue;
			}

			// The descriptor layout is compile-time data; the driver is the one that has to
			// agree. Check it once per shader, on the first linked variant, so a mismatch is a
			// line in the log rather than a corrupted image.
			if (!blockValidated)
			{
				ValidateParameterBlock(p_package, *program);
				blockValidated = true;
			}

			const Rendering::Data::FeatureSet features(variant.features.begin(), variant.features.end());
			variants[variant.pass].emplace(features, std::move(program));
		}

		if (variants.empty() && p_error != nullptr && p_error->empty())
		{
			*p_error = "the package produced no usable variant";
		}

		return variants;
	}

	/** The package's parameter records, in the runtime's form.
	 *
	 * The package is the load-time format; Shader keeps this list so Material can describe its
	 * properties from the material the package was built from instead of asking the driver
	 * which uniforms stayed active (see Shader::HasParameterTable). */
	std::vector<Rendering::Resources::Shader::Parameter> RuntimeParameters(
		const Rendering::Resources::ShaderPackage& p_package)
	{
		std::vector<Rendering::Resources::Shader::Parameter> parameters;
		parameters.reserve(p_package.Parameters().size());

		for (const Rendering::Resources::ShaderPackage::Parameter& parameter : p_package.Parameters())
		{
			parameters.push_back(Rendering::Resources::Shader::Parameter{
				.name = parameter.name,
				.type = parameter.glslType,
				.defaultValue = parameter.defaultValue,
				.label = parameter.label,
				.group = parameter.group,
				.min = parameter.min,
				.max = parameter.max,
				.step = parameter.step,
				.count = parameter.count,
				.offset = parameter.offset,
				.stride = parameter.stride,
				.binding = parameter.binding,
				.isSampler = parameter.isSampler,
				.hasRange = parameter.hasRange,
				.hasStep = parameter.hasStep,
				.isColor = parameter.isColor,
				.isUi = parameter.isUi });
		}

		return parameters;
	}

	/** The material block's state, as the runtime's optional overrides.
	 *
	 * The package keeps the values as the material's text ("back", "transparent", "false");
	 * the runtime works in booleans. Leaving a field unset means the material did not declare
	 * it, and the material instance keeps the engine's default for that field. */
	Rendering::Resources::Shader::State RuntimeState(
		const Rendering::Resources::ShaderPackage& p_package)
	{
		Rendering::Resources::Shader::State state;
		const std::map<std::string, std::string>& states = p_package.States();

		if (const auto it = states.find("blending"); it != states.end())
		{
			// filament's names: opaque | transparent | add | multiply | screen | fade. "masked"
			// clips in the shader and blends like opaque, so it lands on OPAQUE too.
			using Rendering::Settings::EBlendMode;
			state.blendable = it->second != "opaque";
			state.blendMode =
				it->second == "transparent" ? EBlendMode::BLEND_TRANSPARENT :
				it->second == "add" ? EBlendMode::BLEND_ADD :
				it->second == "multiply" ? EBlendMode::BLEND_MULTIPLY :
				it->second == "screen" ? EBlendMode::BLEND_SCREEN :
				it->second == "fade" ? EBlendMode::BLEND_FADE :
				EBlendMode::BLEND_OPAQUE;
		}
		if (const auto it = states.find("culling"); it != states.end())
		{
			state.backfaceCulling = it->second == "back";
			state.frontfaceCulling = it->second == "front";
		}
		if (const auto it = states.find("depthWrite"); it != states.end())
		{
			state.depthWriting = it->second == "true";
		}

		return state;
	}
}

namespace Rendering::Resources::Loaders
{
	ShaderLoader::LoggingSettings ShaderLoader::GetLoggingSettings()
	{
		return __LOGGING_SETTINGS;
	}

	void ShaderLoader::SetLoggingSettings(LoggingSettings p_settings)
	{
		__LOGGING_SETTINGS = p_settings;
	}

	Shader* ShaderLoader::Create(const std::string& p_filePath, FilePathParserCallback p_pathParser)
	{
		// A shader the build already compiled (MoonMatc, see docs/ShaderPipeline.md) comes
		// out of the archive as a package: nothing to parse, just compile what it lists.
		if (const ShaderFile* file = Rendering::Resources::FindShaderFile(p_filePath))
		{
			if (Rendering::Resources::ShaderPackage::IsPackage(file->data, file->size))
			{
				const Rendering::Resources::ShaderPackage package(file->data, file->size);
				if (package.IsValid())
				{
					Shader* shader = new Shader(p_filePath, AssembleFromPackage(package), RuntimeParameters(package));
					shader->SetState(RuntimeState(package));
					shader->SetParameterBlockSize(package.ParameterBlockSize());
					return shader;
				}
			}
		}

		auto result = CompileShaderFromFile(p_filePath, p_pathParser);
		return new Shader(p_filePath, std::move(result.variants));
	}

	Shader* ShaderLoader::CreateFromPackage(const void* p_data, uint32_t p_size,
		const std::string& p_name, std::string* p_error)
	{
		if (p_data == nullptr || p_size == 0)
		{
			if (p_error != nullptr) { *p_error = "empty package"; }
			return nullptr;
		}

		const Rendering::Resources::ShaderPackage package(p_data, p_size);
		if (!package.IsValid())
		{
			if (p_error != nullptr) { *p_error = "not a valid shader package"; }
			return nullptr;
		}

		std::string error;
		auto variants = AssembleFromPackage(package, &error);
		if (variants.empty())
		{
			if (p_error != nullptr) { *p_error = std::move(error); }
			return nullptr;
		}

		if (p_error != nullptr) { p_error->clear(); }
		Shader* shader = new Shader(p_name, std::move(variants), RuntimeParameters(package));
		shader->SetState(RuntimeState(package));
		shader->SetParameterBlockSize(package.ParameterBlockSize());
		return shader;
	}

	Shader* ShaderLoader::CreateFromSource(const std::string& p_vertexShader, const std::string& p_fragmentShader, const std::string& p_geomertyShader)
	{
		auto result = CompileShaderFromSources(p_vertexShader, p_fragmentShader, p_geomertyShader);
		return new Shader({}, std::move(result.variants));
	}

	Shader* ShaderLoader::CreateFromSource(const std::string& p_sourceShader, Rendering::Resources::Loaders::ShaderLoader::FilePathParserCallback p_pathParser)
	{
		auto shaderLoadResult =LoadShader(p_sourceShader,p_pathParser);
		ShaderParseResult shaderParseResult=ParseShader(shaderLoadResult);
		auto result=AssembleShader(shaderParseResult);
		return new Shader("", std::move(result.variants));
	}

	void ShaderLoader::Recompile(Shader& p_shader, const std::string& p_filePath, FilePathParserCallback p_pathParser)
	{
		auto result = CompileShaderFromFile(p_filePath, p_pathParser);

		if (result.failures == 0)
		{
			p_shader.SetVariants(std::move(result.variants));
		}
		else
		{
			std::cout << (std::format(
				"[Shader Reload] {}: reloading failed, previous shader programs kept.",
				result.inputInfo.name
			)) << std::endl;;
		}
	}

	bool ShaderLoader::RecompileFromPackage(Shader& p_shader, const void* p_data, uint32_t p_size,
		std::string* p_error)
	{
		if (p_data == nullptr || p_size == 0)
		{
			if (p_error != nullptr) { *p_error = "empty package"; }
			return false;
		}

		const Rendering::Resources::ShaderPackage package(p_data, p_size);
		if (!package.IsValid())
		{
			if (p_error != nullptr) { *p_error = "not a valid shader package"; }
			return false;
		}

		std::string error;
		auto variants = AssembleFromPackage(package, &error);
		if (variants.empty())
		{
			// Keep what the shader had: a broken edit must not blank out the preview or the scene.
			if (p_error != nullptr) { *p_error = error.empty() ? "the package has no stages" : std::move(error); }
			return false;
		}

		p_shader.SetVariants(std::move(variants));
		p_shader.SetParameters(RuntimeParameters(package));
		p_shader.SetState(RuntimeState(package));
		p_shader.SetParameterBlockSize(package.ParameterBlockSize());
		if (p_error != nullptr) { p_error->clear(); }
		return true;
	}

	bool ShaderLoader::Destroy(Shader*& p_shader)
	{
		if (p_shader)
		{
			delete p_shader;
			p_shader = nullptr;
			return true;
		}

		return false;
	}
}
