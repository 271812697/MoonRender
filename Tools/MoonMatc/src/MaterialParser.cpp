// The material file parser: the .shader / .mat text is split into its blocks and each block is
// handed to its processor - filament-matp's MaterialParser / mConfigProcessor shape. Every
// processor writes into the MaterialBuilder the caller owns.
//
// MaterialParser.h declares the class; the block readers themselves (StripComments, LineOf,
// TakeBlock, ParseMaterialBlock) live in this file's anonymous namespace because nothing else
// needs them. Split out of Matc.cpp - see docs/MaterialPipeline.md.

#include "MaterialParser.h"

#include <cctype>
#include <sstream>
#include <unordered_map>

#include "ParametersProcessor.h"
#include "TextUtils.h"

namespace Moon::Matc
{

	namespace
	{
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

	/** The 1-based line a fragment of the source starts on: the #line directives handed to
	 * the driver point back here, so an error names the .shader line the user wrote. */
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

	void MaterialParser::ProcessBlock(const std::string& p_keyword, const std::string& p_body)
	{
		const auto& processors = ConfigProcessors();
		const auto it = processors.find(p_keyword);
		if (it != processors.end())
		{
			(this->*(it->second))(p_body);
		}
	}

	void MaterialParser::ProcessMaterial(const std::string& p_body)
	{
		ParametersProcessor processor;
		processor.ProcessBlock(p_body, *m_builder);
	}

	void MaterialParser::ProcessVertex(const std::string& p_body)
	{
		m_builder->vertexBody = Trim(p_body);
		m_builder->vertexEntryLine = LineOf(*m_text, m_builder->vertexBody);
	}

	void MaterialParser::ProcessGeometry(const std::string& p_body)
	{
		m_builder->geometryBody = Trim(p_body);
		m_builder->geometryEntryLine = LineOf(*m_text, m_builder->geometryBody);
	}

	void MaterialParser::ProcessFragment(const std::string& p_body)
	{
		m_builder->fragmentBody = Trim(p_body);
		m_builder->fragmentEntryLine = LineOf(*m_text, m_builder->fragmentBody);
	}

	bool MaterialParser::Parse(const std::string& p_source, MaterialBuilder& p_builder, std::string& p_error)
	{
		m_builder = &p_builder;

		const std::string text = StripComments(p_source);
		m_text = &text;

		std::string body;
		size_t next = 0;

		// material { } - required, and the first block a file carries.
		if (!TakeBlock(text, "material", 0, body, next))
		{
			p_error = "no material { } block";
			return false;
		}
		ProcessBlock("material", body);

		size_t afterVertex = 0;
		if (TakeBlock(text, "vertex", next, body, next))
		{
			afterVertex = next;
			ProcessBlock("vertex", body);
		}

		if (TakeBlock(text, "fragment", next, body, next))
		{
			ProcessBlock("fragment", body);
		}

		// The geometry stage: our extension, and the old files put it between the vertex and
		// the fragment sections - so it is searched from the end of the vertex block, not from
		// wherever the fragment block ended. The legacy loader's marker is spelled
		// "#shader geomerty", so the block keyword is accepted either way.
		size_t geometryNext = 0;
		if (TakeBlock(text, "geometry", afterVertex, body, geometryNext)
			|| TakeBlock(text, "geomerty", afterVertex, body, geometryNext))
		{
			ProcessBlock("geometry", body);
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
