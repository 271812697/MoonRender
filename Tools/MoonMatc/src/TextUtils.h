#pragma once
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>

/** Small text helpers the material parser and the parameter processor both need. They used to
 * live in Matc.cpp's anonymous namespace; they are inline here so that the files the parser is
 * being split into (filament-matp's shape: MaterialParser / ParametersProcessor / MaterialBuilder)
 * can share them without a .cpp of their own. */
namespace Moon::Matc
{
	inline std::string Trim(std::string_view p_text)
	{
		size_t begin = 0;
		size_t end = p_text.size();
		while (begin < end && std::isspace(static_cast<unsigned char>(p_text[begin]))) { ++begin; }
		while (end > begin && std::isspace(static_cast<unsigned char>(p_text[end - 1]))) { --end; }
		return std::string(p_text.substr(begin, end - begin));
	}

	/** A quoted material value ("Base Color") without its quotes; an unquoted one is returned
	 * as it is. Labels have spaces in them, so they are the fields that get quoted. */
	inline std::string Unquote(const std::string& p_text)
	{
		if (p_text.size() >= 2 && p_text.front() == '"' && p_text.back() == '"')
		{
			return p_text.substr(1, p_text.size() - 2);
		}
		return p_text;
	}

	/** A numeric value out of the material file. False when the text is not a whole number, in
	 * which case the designer field keeps its default. */
	inline bool ParseFloatText(const std::string& p_text, float& p_value)
	{
		if (p_text.empty())
		{
			return false;
		}
		char* end = nullptr;
		p_value = std::strtof(p_text.c_str(), &end);
		return end != p_text.c_str() && *end == '\0';
	}
}
