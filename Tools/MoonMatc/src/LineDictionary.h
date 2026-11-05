#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/** The package's line table (filament's LineDictionary). Every distinct line of the shared stage
 * texts is stored once; a stage is then a sequence of ids. The prelude is identical in every
 * stage and every material, so the duplicated lines collapse into one entry - without it the
 * archive carries the same 60 KB prelude once per stage.
 *
 * Encoding splits on '\n' only (no trimming): a text that ends with a newline keeps its empty
 * last segment, so decoding is exact. */
namespace Moon::Matc
{
	class LineDictionary
	{
	public:
		/** The id of p_line, adding it to the table the first time it is seen. */
		uint32_t Add(const std::string& p_line);

		/** The ids of p_text's lines, in order. */
		std::vector<uint32_t> Encode(const std::string& p_text);

		const std::vector<std::string>& Lines() const { return m_lines; }
		size_t Size() const { return m_lines.size(); }

	private:
		std::vector<std::string> m_lines;
		std::unordered_map<std::string, uint32_t> m_index;
	};
}
