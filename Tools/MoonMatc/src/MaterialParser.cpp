// The material file parser: the .shader / .mat text is split into its blocks and each block is
// handed to its processor - filament-matp's MaterialParser / mConfigProcessor shape. Every
// processor writes into the MaterialBuilder the caller owns.
//
// MaterialParser.h declares the class; MaterialLexer does the scanning (comments, identifiers,
// brace-balanced blocks and line numbers), so there is no separate StripComments / TakeBlock
// pass here. Split out of Matc.cpp - see docs/MaterialPipeline.md.

#include "MaterialParser.h"

#include <cctype>
#include <unordered_map>

#include "JsonishLexer.h"
#include "JsonishParser.h"
#include "MaterialLexeme.h"
#include "MaterialLexer.h"
#include "ParametersProcessor.h"
#include "TextUtils.h"

namespace Moon::Matc
{
	namespace
	{
		/** The line of the first non-whitespace character inside a block. The block lexeme
		 * starts on the line of its '{'; every line break between the brace and the body's
		 * first code character moves the #line anchor down with it. */
		uint32_t BodyLine(const MaterialLexeme& p_block)
		{
			uint32_t line = static_cast<uint32_t>(p_block.getLine());
			for (const char* at = p_block.getStart() + 1; at < p_block.getEnd() && std::isspace(static_cast<unsigned char>(*at)); ++at)
			{
				if (*at == '\n')
				{
					++line;
				}
			}
			return line;
		}
	}

	const std::unordered_map<std::string, MaterialParser::ConfigProcessor>& MaterialParser::ConfigProcessors()
	{
		static const std::unordered_map<std::string, ConfigProcessor> processors = {
			{ "material", &MaterialParser::ProcessMaterial },
			{ "vertex", &MaterialParser::ProcessVertex },
			{ "geometry", &MaterialParser::ProcessGeometry },
			{ "fragment", &MaterialParser::ProcessFragment },
		};
		return processors;
	}

	void MaterialParser::ProcessBlock(const std::string& p_keyword, const std::string& p_body, uint32_t p_line)
	{
		const auto& processors = ConfigProcessors();
		const auto it = processors.find(p_keyword);
		if (it != processors.end())
		{
			(this->*(it->second))(p_body, p_line);
		}
	}

	void MaterialParser::ProcessMaterial(const std::string& p_body, uint32_t)
	{
		// The parser wants an object including its braces; the block lexeme's body excludes
		// them, so put them back around the body. The lexer/parser then hand the processor a
		// JsonishObject - it never sees the body text again.
		const std::string jsonText = "{" + p_body + "}";
		JsonishLexer lexer;
		lexer.lex(jsonText.data(), jsonText.size());
		JsonishParser parser(lexer.getLexemes());

		const std::unique_ptr<JsonishObject> object = parser.parse();
		if (object == nullptr)
		{
			m_materialParseFailed = true;
			return;
		}

		ParametersProcessor processor;
		processor.ProcessBlock(*object, *m_builder);
	}

	void MaterialParser::ProcessVertex(const std::string& p_body, uint32_t p_line)
	{
		m_builder->vertexBody = Trim(p_body);
		m_builder->vertexEntryLine = p_line;
	}

	void MaterialParser::ProcessGeometry(const std::string& p_body, uint32_t p_line)
	{
		m_builder->geometryBody = Trim(p_body);
		m_builder->geometryEntryLine = p_line;
	}

	void MaterialParser::ProcessFragment(const std::string& p_body, uint32_t p_line)
	{
		m_builder->fragmentBody = Trim(p_body);
		m_builder->fragmentEntryLine = p_line;
	}

	bool MaterialParser::Parse(const std::string& p_source, MaterialBuilder& p_builder, std::string& p_error)
	{
		m_builder = &p_builder;
		m_materialParseFailed = false;
		p_builder.parseError.clear();

		MaterialLexer lexer;
		lexer.lex(p_source.data(), p_source.size());
		const std::vector<MaterialLexeme>& lexemes = lexer.getLexemes();

		// Blocks are an identifier immediately followed by its `{ ... }` lexeme. Everything
		// else the lexer emits (comments are skipped, stray characters are UNKNOWN) is ignored.
		bool hasMaterial = false;
		bool hasVertex = false;
		bool hasGeometry = false;
		bool hasFragment = false;

		for (size_t i = 0; i + 1 < lexemes.size(); ++i)
		{
			if (lexemes[i].getType() != MaterialType::IDENTIFIER
				|| lexemes[i + 1].getType() != MaterialType::BLOCK)
			{
				continue;
			}

			const MaterialLexeme& block = lexemes[i + 1];
			const std::string keyword = lexemes[i].getStringValue();
			const std::string body(block.getStart() + 1, block.getEnd());
			const uint32_t line = BodyLine(block);

			// The legacy loader's marker is spelled "#shader geomerty", so the misspelling is
			// accepted as the same block. Duplicate blocks keep the first one, as the old
			// sequential search did.
			if (keyword == "material" && !hasMaterial)
			{
				ProcessBlock("material", body, line);
				hasMaterial = true;
			}
			else if (keyword == "vertex" && !hasVertex)
			{
				ProcessBlock("vertex", body, line);
				hasVertex = true;
			}
			else if ((keyword == "geometry" || keyword == "geomerty") && !hasGeometry)
			{
				ProcessBlock("geometry", body, line);
				hasGeometry = true;
			}
			else if (keyword == "fragment" && !hasFragment)
			{
				ProcessBlock("fragment", body, line);
				hasFragment = true;
			}

			++i; // skip the block lexeme as well
		}

		// material { } - required, and the first block a file carries.
		if (!hasMaterial)
		{
			p_error = "no material { } block";
			return false;
		}
		if (m_materialParseFailed)
		{
			p_error = "cannot parse material { } block";
			return false;
		}
		if (!p_builder.parseError.empty())
		{
			p_error = p_builder.parseError;
			return false;
		}
		if (p_builder.name.empty())
		{
			p_error = "material { name : ... } is required";
			return false;
		}
		if (p_builder.fragmentBody.empty())
		{
			p_error = "a fragment { } block is required";
			return false;
		}
		return true;
	}
}
