#pragma once
#include <string>
#include <unordered_map>

#include "MaterialBuilder.h"

/** filament-matp's MaterialParser, in the shape this pipeline needs.
 *
 * A .shader (and, later, a .mat) is a handful of named blocks: `material { }` declares the
 * parameters and the render state, `vertex` / `geometry` / `fragment` carry the stage code. The
 * parser strips comments, finds each block and dispatches it through mConfigProcessor to the
 * member that owns it; every processor writes into the MaterialBuilder the caller passes in, so
 * the parser itself keeps no material state.
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
		using ConfigProcessor = void (MaterialParser::*)(const std::string& p_body);

		/** mConfigProcessor: block keyword -> the parser member that handles that block. */
		static const std::unordered_map<std::string, ConfigProcessor>& ConfigProcessors();

		void ProcessBlock(const std::string& p_keyword, const std::string& p_body);

		void ProcessMaterial(const std::string& p_body);
		void ProcessVertex(const std::string& p_body);
		void ProcessGeometry(const std::string& p_body);
		void ProcessFragment(const std::string& p_body);

		MaterialBuilder* m_builder = nullptr;
		/** The comment-stripped source, for the #line number each block starts on. */
		const std::string* m_text = nullptr;
	};
}
