#pragma once
#include <cstdint>
#include <string>
#include <vector>

/** The PerMaterial descriptor layout: which binding point binds what kind of resource,
 * independent of the resources themselves (filament's DescriptorSetLayout).
 *
 * A Material owns one; a MaterialInstance fills descriptor sets against it. The descriptor
 * array is indexed by the original binding point; the OpenGLProgram binding map decides which
 * GL slot that binding point maps to for a given shader. */
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

		/** Whether a resource of that kind may live at that binding point. */
		bool Validate(Kind p_kind, uint32_t p_binding) const;

		uint32_t MaxBinding() const;

	private:
		std::vector<Binding> m_bindings;
	};
}
