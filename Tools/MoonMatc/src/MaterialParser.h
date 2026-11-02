#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

#include "MaterialBuilder.h"

/** filament-matp's MaterialParser, in the shape this pipeline needs.
 *
 * A .shader (and, later, a .mat) is a handful of named blocks: `material { }` declares the
 * parameters and the render state, `vertex` / `geometry` / `fragment` carry the stage code. The
 * parser lexes the file with MaterialLexer (blocks, identifiers, comments, line numbers) and
 * dispatches each block through mConfigProcessor to the member that owns it; every processor
 * writes into the MaterialBuilder the caller passes in, so the parser itself keeps no material
 * state.
 */
namespace Moon::Matc
{
	class MaterialParser
	{
	public:
		/** Parses p_source into p_builder. False (and p_error) for a file that is missing the
		 * material block, its name, or the fragment stage. */
		bool Parse(const std::string& p_source, MaterialBuilder& p_builder, std::string& p_error);

	private:
		/** A block processor gets the block's body (without its braces) and the line the first
		 * code character of that body sits on - what #line has to name. */
		using ConfigProcessor = void (MaterialParser::*)(const std::string& p_body, uint32_t p_line);

		/** mConfigProcessor: block keyword -> the parser member that handles that block. */
		static const std::unordered_map<std::string, ConfigProcessor>& ConfigProcessors();

		void ProcessBlock(const std::string& p_keyword, const std::string& p_body, uint32_t p_line);

		void ProcessMaterial(const std::string& p_body, uint32_t p_line);
		void ProcessVertex(const std::string& p_body, uint32_t p_line);
		void ProcessGeometry(const std::string& p_body, uint32_t p_line);
		void ProcessFragment(const std::string& p_body, uint32_t p_line);

		MaterialBuilder* m_builder = nullptr;
		/** True when the material block was found but could not be parsed into a JsonishObject. */
		bool m_materialParseFailed = false;
	};
}
