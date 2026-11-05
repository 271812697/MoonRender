#include <Rendering/Data/DescriptorSetLayout.h>

#include <algorithm>

namespace Rendering::Data
{
	void DescriptorSetLayout::Add(std::string p_name, Kind p_kind, uint32_t p_binding)
	{
		m_bindings.push_back(Binding{ std::move(p_name), p_kind, p_binding });
	}

	const DescriptorSetLayout::Binding* DescriptorSetLayout::Find(const std::string& p_name) const
	{
		const auto it = std::find_if(m_bindings.begin(), m_bindings.end(),
			[&p_name](const Binding& p_binding) { return p_binding.name == p_name; });
		return it != m_bindings.end() ? &*it : nullptr;
	}

	bool DescriptorSetLayout::Validate(Kind p_kind, uint32_t p_binding) const
	{
		return std::any_of(m_bindings.begin(), m_bindings.end(),
			[&](const Binding& p_entry)
			{
				return p_entry.binding == p_binding && p_entry.kind == p_kind;
			});
	}

	uint32_t DescriptorSetLayout::MaxBinding() const
	{
		uint32_t max = 0;
		for (const Binding& binding : m_bindings)
		{
			max = std::max(max, binding.binding);
		}
		return max;
	}
}
