#pragma once
#include <functional>
#include <string>
#include <vector>

/** MoonMatc as a library, so the editor can compile a material it is editing without going
 * through the command line tool: same parser, same package, same chunks. The executable in
 * main.cpp is a thin argument parser over these two entry points.
 *
 * Nothing here touches GL: a package is text and tables. Compiling it for real is what the
 * runtime does when the texture is first used (and what the build validates with glslang). */
namespace Moon::Matc
{
	/** Supplies the text of one #include. A release build has no Shaders folder, so the editor
	 * backs this with the shader archive; return false to let the include directories on disk
	 * handle the request (the normal build-time behaviour). */
	using IncludeResolver = std::function<bool(
		const std::string& p_name, std::string& p_text, std::string& p_origin)>;

	struct CompileOptions
	{
		/** The name that goes into `#line` directives and messages, e.g. "Grid.shader". */
		std::string displayName;
		/** Where to write the package. Empty keeps it only in CompileResult::package. */
		std::string outputPath;
		/** Where to dump one .glsl per (pass, variant, stage). Empty means no dump. */
		std::string dumpGlslDir;
		/** Where to write the reflection JSON. Empty means no JSON. */
		std::string dumpJsonPath;
		/** Where to write the Filament-style variant enumeration (build-time diagnostic). */
		std::string dumpVariantsPath;
		/** When non-empty, compile exactly this feature set instead of the material''s feature
		 * power set. The material designer uses it to recompile a material after a feature
		 * toggle (filament''s model: features are fixed at compile time). */
		std::vector<std::string> enabledFeatures;
		/** The shader roots: `#include ":Shaders/..."` and plain relative includes resolve
		 * against these, in order. */
		std::vector<std::string> includeDirs;
		/** Optional source provider for #include, tried before includeDirs. Empty keeps the
		 * build-time behaviour (files on disk only). */
		IncludeResolver includeResolver;
		/** Debug aid: stop after building the stages and hand the first one back instead of
		 * packaging. */
		bool preprocessOnly = false;
	};

	struct CompileResult
	{
		bool success = false;
		/** True when the source has no material { } block: it was copied through as it was, so
		 * the runtime still compiles it the old way. */
		bool passthrough = false;
		/** The .ovshader bytes (or, for a passthrough, the source text). */
		std::string package;
		/** The reflection JSON, when the material produced one. */
		std::string json;
		/** The first stage's source, when CompileOptions::preprocessOnly was set. */
		std::string preprocessed;
		/** Why it failed, empty on success. */
		std::string error;
		/** For the CLI's one-line summary. */
		std::string materialName;
		uint32_t variantCount = 0;
	};

	/** Compiles one material source. */
	CompileResult CompileSource(const std::string& p_sourceText, const CompileOptions& p_options);

	/** Compiles one material file. `displayName` defaults to the file name. */
	CompileResult CompileFile(const std::string& p_path, CompileOptions p_options);
}
