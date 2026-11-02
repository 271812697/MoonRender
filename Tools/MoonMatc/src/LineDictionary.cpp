#include "LineDictionary.h"

namespace Moon::Matc
{
	uint32_t LineDictionary::Add(const std::string& p_line)
	{
		const auto it = m_index.find(p_line);
		if (it != m_index.end())
		{
			return it->second;
		}

		const uint32_t id = static_cast<uint32_t>(m_lines.size());
		m_lines.push_back(p_line);
		m_index.emplace(p_line, id);
		return id;
	}

	std::vector<uint32_t> LineDictionary::Encode(const std::string& p_text)
	{
		std::vector<uint32_t> ids;
		size_t begin = 0;
		while (true)
		{
			const size_t end = p_text.find('\n', begin);
			if (end == std::string::npos)
			{
				ids.push_back(Add(p_text.substr(begin)));
				break;
			}
			ids.push_back(Add(p_text.substr(begin, end - begin)));
			begin = end + 1;
		}
		return ids;
	}
}
