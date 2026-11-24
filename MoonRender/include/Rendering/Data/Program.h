#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <Rendering/Backend/ResourceHandle.h>
#include <Rendering/Data/ProgramDescriptorBindings.h>
#include <Rendering/HAL/ShaderStage.h>
#include <Rendering/Settings/ShaderLinkingResult.h>

namespace Rendering::Backend
{
	struct HwProgram;
}

namespace Rendering::Data
{
	/** The front end of a shader program: what a material owns.
	 *
	 * It holds the handle that names the backend object - HAL::ShaderProgram, which lives in the
	 * handle's slot on the side that owns the context and is the only thing that has a GL program -
	 * plus the stages it was given, the descriptor layout the material resolved and the slot numbers
	 * that layout produces (pure bookkeeping). Link() records the build; Bind() records the bind and
	 * the lazy slot declaration. The counterpart of Data::BufferObject.
	 */
	class Program
	{
	public:
		/** Reserves the handle. The backend object (and the GL program) is made by Link(). */
		Program();

		explicit Program(const Rendering::Data::DescriptorSetInfo& p_bindings);

		/** Frees the record now and queues the GL deletion to the side that owns the context. */
		~Program();

		Program(const Program&) = delete;
		Program& operator=(const Program&) = delete;
		Program(Program&& p_other) noexcept;
		Program& operator=(Program&& p_other) noexcept;

		/** Keeps the stage until the program is linked (Link carries the sources with its command). */
		void Attach(const Rendering::HAL::ShaderStage& p_shader);
		void Detach(const Rendering::HAL::ShaderStage& p_shader);
		void DetachAll();

		/** Builds the program: with a driver the sources travel with the create-program command and
		 * the build runs where the context is; without one it runs here. */
		Settings::ShaderLinkingResult Link();

		/** Binds the program, declaring the descriptor slots to it first when they were not yet. */
		void Bind();
		void Unbind() const;

		/** The GL name, resolved on this side; 0 until the create command has run. */
		uint32_t GetID() const;

		/** Stores the descriptor layout; Bind() resolves it lazily on first use. */
		void SetDescriptorBindings(const Rendering::Data::DescriptorSetInfo& p_bindings);

		bool HasDescriptorBindings() const { return m_descriptorSlotsInitialized; }

		/** Which descriptor slot the layout gave a binding (set 2 uses a map, sets 0/1 the engine's
		 * fixed table). */
		uint32_t GetDescriptorSlot(uint32_t p_set, uint32_t p_binding) const;

		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwProgram>& GetHandle() const
		{
			return m_handle;
		}

	private:
		/** Fills m_descriptorSlots from the layout. Pure bookkeeping: the numbers come from the
		 * engine's fixed table and the per-material counters, never from GL. */
		void ComputeDescriptorSlots(const Rendering::Data::DescriptorSetInfo& p_bindings);

		Rendering::Backend::ResourceHandle<Rendering::Backend::HwProgram> m_handle;
		std::vector<std::reference_wrapper<const Rendering::HAL::ShaderStage>> m_attachedShaders;
		std::unordered_map<uint64_t, uint32_t> m_descriptorSlots;
		bool m_descriptorSlotsInitialized = false;
		bool m_descriptorBindingsDeclared = false;
		Rendering::Data::DescriptorSetInfo m_descriptorBindings{};
		bool m_descriptorBindingsConfigured = false;
	};
}
