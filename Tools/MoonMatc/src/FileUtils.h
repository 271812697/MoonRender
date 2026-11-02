#pragma once
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

/** File helpers shared by the compiler's modules (Matc.cpp reads its input, Includes.cpp reads
 * the headers an #include points at). */
namespace Moon::Matc
{
	inline std::string ReadFile(const std::filesystem::path& p_path, bool& p_ok)
	{
		std::ifstream stream(p_path, std::ios::binary);
		p_ok = static_cast<bool>(stream);
		if (!p_ok)
		{
			return {};
		}
		return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
	}
}
