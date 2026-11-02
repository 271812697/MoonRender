#pragma once
#include <cstdint>
#include <string>
#include <vector>

/** The PerMaterial descriptor layout: which slot binds what kind of resource, independent of the
 * resources themselves (filament's DescriptorSetLayout).
 *
 * A Material owns one, built from the package's UboBlockInfo / SamplerBlockInfo; a
 * MaterialInstance fills a DescriptorSet against it, and the DescriptorSet validates every
 * insertion against this layout, so a texture can never land in a uniform-block slot. */
namespace Rendering::Data
{
	class DescriptorSetLayout
	{
	public:
		enum class Kind : uint8_t
		{
			UniformBuffer = 0,
			StorageBuffer = 1,
			Texture = 2,
		};

		struct Binding
		{
			std::string name;      // the parameter name ("u_AlbedoMap", the block's name)
			Kind kind = Kind::Texture;
			uint32_t binding = 0;  // the block binding point or the texture unit
		};

		void Add(std::string p_name, Kind p_kind, uint32_t p_binding);

		bool IsEmpty() const { return m_bindings.empty(); }
		const std::vector<Binding>& Bindings() const { return m_bindings; }

		/** The entry with that name, or nullptr when the layout has none. */
		const Binding* Find(const std::string& p_name) const;

		/** Whether a resource of that kind may be bound at that slot - what DescriptorSet::Set
		 * checks before it stores anything. */
		bool Validate(Kind p_kind, uint32_t p_binding) const;

		/** The highest slot used, so a backend can size its binding table. */
		uint32_t MaxBinding() const;

	private:
		std::vector<Binding> m_bindings;
	};
}
