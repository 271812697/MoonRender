#include <Rendering/Data/Program.h>

#include <algorithm>
#include <memory>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/HandleAllocator.h>
#include <Rendering/Backend/ProgramRequests.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/Data/EngineDescriptorSets.h>
#include <Rendering/HAL/Common/HALDriverAccess.h>
#include <Rendering/HAL/ShaderProgram.h>

namespace Rendering::Data
{
	namespace
	{
		Rendering::HAL::ShaderProgram* Resolve(
			const Rendering::Backend::ResourceHandle<Rendering::Backend::HwProgram>& p_handle)
		{
			return Rendering::Backend::GetHandleAllocator()
				.HandleCast<Rendering::HAL::ShaderProgram>(p_handle);
		}
	}

	Program::Program()
	{
		// Filament's createProgramS half: the slot is reserved here, without GL; the build (the
		// create-program command) makes the program object where the context is.
		m_handle = Rendering::Backend::GetHandleAllocator()
			.Allocate<Rendering::Backend::HwProgram>();
	}

	Program::Program(const Rendering::Data::DescriptorSetInfo& p_bindings) : Program()
	{
		SetDescriptorBindings(p_bindings);
	}

	Program::~Program()
	{
		Rendering::Backend::Records::DestroyProgram(m_handle);
	}

	Program::Program(Program&& p_other) noexcept
		: m_handle(p_other.m_handle)
		, m_attachedShaders(std::move(p_other.m_attachedShaders))
		, m_descriptorSlots(std::move(p_other.m_descriptorSlots))
		, m_descriptorSlotsInitialized(p_other.m_descriptorSlotsInitialized)
		, m_descriptorBindingsDeclared(p_other.m_descriptorBindingsDeclared)
		, m_descriptorBindings(p_other.m_descriptorBindings)
		, m_descriptorBindingsConfigured(p_other.m_descriptorBindingsConfigured)
	{
		p_other.m_handle.clear();
		p_other.m_attachedShaders.clear();
	}

	Program& Program::operator=(Program&& p_other) noexcept
	{
		if (this != &p_other)
		{
			Rendering::Backend::Records::DestroyProgram(m_handle);

			m_handle = p_other.m_handle;
			m_attachedShaders = std::move(p_other.m_attachedShaders);
			m_descriptorSlots = std::move(p_other.m_descriptorSlots);
			m_descriptorSlotsInitialized = p_other.m_descriptorSlotsInitialized;
			m_descriptorBindingsDeclared = p_other.m_descriptorBindingsDeclared;
			m_descriptorBindings = p_other.m_descriptorBindings;
			m_descriptorBindingsConfigured = p_other.m_descriptorBindingsConfigured;

			p_other.m_handle.clear();
			p_other.m_attachedShaders.clear();
		}
		return *this;
	}

	void Program::Attach(const Rendering::HAL::ShaderStage& p_shader)
	{
		m_attachedShaders.emplace_back(p_shader);
	}

	void Program::Detach(const Rendering::HAL::ShaderStage& p_shader)
	{
		m_attachedShaders.erase(
			std::remove_if(m_attachedShaders.begin(), m_attachedShaders.end(),
				[&p_shader](const std::reference_wrapper<const Rendering::HAL::ShaderStage>& p_entry)
				{
					return &p_entry.get() == &p_shader;
				}),
			m_attachedShaders.end());
	}

	void Program::DetachAll()
	{
		m_attachedShaders.clear();
	}

	Settings::ShaderLinkingResult Program::Link()
	{
		using namespace Rendering::Backend;

		// The sources travel with the command: this side never compiles (glCompileShader needs the
		// context, which may be the executor's).
		auto stages = std::make_shared<std::vector<ShaderSourceSpec>>();
		stages->reserve(m_attachedShaders.size());
		for (const std::reference_wrapper<const Rendering::HAL::ShaderStage>& stage : m_attachedShaders)
		{
			ShaderSourceSpec spec;
			spec.type = stage.get().GetShaderType();
			spec.source = std::make_shared<const std::string>(stage.get().GetSource());
			stages->push_back(std::move(spec));
		}

		CreateProgramRequest request;
		request.programHandleId = m_handle.GetId();
		request.stages = std::move(stages);

		::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
		if (m_descriptorBindingsConfigured)
		{
			request.bindings = std::make_shared<const Rendering::Data::DescriptorSetInfo>(
				m_descriptorBindings);
		}

		// A new link means a new program: the binding map is rebuilt on first bind.
		const auto resetSlots = [this](bool p_declared)
		{
			m_descriptorSlots.clear();
			m_descriptorSlotsInitialized = false;
			m_descriptorBindingsDeclared = p_declared;
		};

		if (driver != nullptr)
		{
			driver->CreateProgram(std::move(request));
			// The create command declares the slots right after the link, so this side does not.
			resetSlots(true);
			return { .success = true };
		}

		// No other side to run the build on: create the object here and build it in place.
		Records::CreateProgram(m_handle);

		Rendering::HAL::ShaderProgram* const program = Resolve(m_handle);
		if (program == nullptr)
		{
			resetSlots(false);
			return { .success = false, .message = "the program object could not be created" };
		}

		Settings::ShaderLinkingResult result = program->Build(*request.stages);
		if (m_descriptorBindingsConfigured)
		{
			program->DeclareBindings(m_descriptorBindings);
		}
		resetSlots(m_descriptorBindingsConfigured);

		return result;
	}

	void Program::Bind()
	{
		// Filament's token idea: the program resolves its descriptor slots on first use, inside the
		// program. Materials only select and bind it.
		if (!m_descriptorSlotsInitialized && m_descriptorBindingsConfigured)
		{
			ComputeDescriptorSlots(m_descriptorBindings);
		}

		::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
		if (driver != nullptr)
		{
			// glUseProgram and the slot declaration belong to the executor. The declaration is lazy,
			// like the immediate path's: a material's layout can be configured after the link.
			if (!m_descriptorBindingsDeclared && m_descriptorBindingsConfigured)
			{
				Rendering::Backend::DeclareProgramBindingsRequest request;
				request.programHandleId = m_handle.GetId();
				request.bindings = std::make_shared<const Rendering::Data::DescriptorSetInfo>(
					m_descriptorBindings);
				driver->DeclareProgramBindings(std::move(request));
				m_descriptorBindingsDeclared = true;
			}

			// By handle, not by name: the GL name is only known after the create command has run,
			// so asking for it here gave 0 and recorded "bind nothing" (docs/RenderThreadPlan.md
			// §8.64).
			driver->BindProgramHandle(m_handle.GetId());
			return;
		}

		Rendering::HAL::ShaderProgram* const program = Resolve(m_handle);
		if (program == nullptr)
		{
			return;
		}

		if (!m_descriptorBindingsDeclared && m_descriptorBindingsConfigured)
		{
			program->DeclareBindings(m_descriptorBindings);
			m_descriptorBindingsDeclared = true;
		}
		program->Bind();
	}

	void Program::Unbind() const
	{
		if (::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver())
		{
			driver->BindProgram(0);
			return;
		}

		if (Rendering::HAL::ShaderProgram* const program = Resolve(m_handle))
		{
			program->Unbind();
		}
	}

	uint32_t Program::GetID() const
	{
		return Rendering::Backend::Records::ProgramName(m_handle);
	}

	void Program::SetDescriptorBindings(const Rendering::Data::DescriptorSetInfo& p_bindings)
	{
		m_descriptorBindings = p_bindings;
		m_descriptorBindingsConfigured = true;
		m_descriptorSlots.clear();
		m_descriptorSlotsInitialized = false;
		m_descriptorBindingsDeclared = false;
	}

	void Program::ComputeDescriptorSlots(const Rendering::Data::DescriptorSetInfo& p_bindings)
	{
		if (m_descriptorSlotsInitialized)
		{
			return;
		}

		// Only the per-material set needs a map: the engine's sets use the fixed slot table, which
		// GetDescriptorSlot reads directly. The numbering here is the same one DeclareBindings hands
		// to GL, and it comes from the layout alone.
		m_descriptorSlots.clear();
		uint32_t bufferSlot = Rendering::Data::EngineDescriptorSets::kFirstMaterialBufferSlot;
		uint32_t textureSlot = Rendering::Data::EngineDescriptorSets::kFirstMaterialTextureSlot;
		for (const Rendering::Data::DescriptorSetLayout::Binding& binding
			: p_bindings[Rendering::Data::kPerMaterialDescriptorSet])
		{
			const bool textureKind =
				binding.kind == Rendering::Data::DescriptorSetLayout::Kind::Texture;
			const uint32_t slot = textureKind ? textureSlot++ : bufferSlot++;
			const uint64_t key =
				(static_cast<uint64_t>(Rendering::Data::kPerMaterialDescriptorSet) << 32) | binding.binding;
			m_descriptorSlots[key] = slot;
		}

		m_descriptorSlotsInitialized = true;
	}

	uint32_t Program::GetDescriptorSlot(uint32_t p_set, uint32_t p_binding) const
	{
		// The engine's sets (0 and 1) have fixed slots and no map.
		if (p_set != Rendering::Data::kPerMaterialDescriptorSet)
		{
			return Rendering::Data::EngineDescriptorSets::SlotOf(p_set, p_binding);
		}

		const uint64_t key = (static_cast<uint64_t>(p_set) << 32) | p_binding;
		const auto it = m_descriptorSlots.find(key);
		return it != m_descriptorSlots.end() ? it->second : UINT32_MAX;
	}
}
