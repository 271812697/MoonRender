#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <Rendering/Backend/CircularBuffer.h>
#include <Rendering/Backend/CommandBufferQueue.h>
#include <Rendering/Backend/BindFramebufferRequest.h>
#include <Rendering/Backend/BufferRequests.h>
#include <Rendering/Backend/DescriptorRequests.h>
#include <Rendering/Backend/ProgramRequests.h>
#include <Rendering/Backend/ReadPixelsRequest.h>
#include <Rendering/Backend/VertexArrayRequests.h>
#include <Rendering/Backend/FramebufferRequests.h>
#include <Rendering/Backend/CreateTextureRequest.h>
#include <Rendering/Backend/OverlayRequests.h>
#include <Rendering/Backend/TextureRequests.h>
#include <Rendering/Backend/DrawMeshRequest.h>
#include <Rendering/Backend/TextureUploadRequest.h>
#include <Rendering/Backend/UpdateTextureRequest.h>
#include <Rendering/Data/PipelineState.h>
#include <Rendering/HAL/Framebuffer.h>
#include <Rendering/Resources/IMesh.h>
#include <Rendering/Settings/EBlendingEquation.h>
#include <Rendering/Settings/EBlendingFactor.h>
#include <Rendering/Settings/ECullFace.h>
#include <Rendering/Settings/ECullingOptions.h>
#include <Rendering/Settings/EComparaisonAlgorithm.h>
#include <Rendering/Settings/EOperation.h>
#include <Rendering/Settings/EPixelDataFormat.h>
#include <Rendering/Settings/EPixelDataType.h>
#include <Rendering/Settings/EPrimitiveMode.h>
#include <Rendering/Settings/ERasterizationMode.h>
#include <Rendering/Settings/ERenderingCapability.h>

namespace Rendering::Backend
{
	class OpenGLDriver;

	/** One recorded command: a function pointer plus the arguments that follow it in the ring.
	 *
	 * Ported from Filament's backend::CommandBase. There is no vtable and no tag switch: the
	 * "what to do" is the pointer this object was constructed with, and the arguments live right
	 * behind it. Run() returns the address of the next command, which the command itself knows
	 * because the writer and the executor agree on Align(sizeof(Command)).
	 */
	class CommandBase
	{
	protected:
		using Execute = void (*)(OpenGLDriver& p_driver, CommandBase* p_command, intptr_t* p_next);

		constexpr explicit CommandBase(Execute p_execute, const char* p_name = nullptr) noexcept
			: m_execute(p_execute), m_name(p_name) {}

	public:
		/** Alignment every command is placed at, and the granularity the next one is found with.
		 * The writer rounds the size up with it and the executor does the same, which is what
		 * keeps the two in step. */
		static constexpr size_t Align(size_t p_size)
		{
			constexpr size_t alignment = alignof(std::max_align_t);
			return (p_size + alignment - 1) & ~(alignment - 1);
		}

		/** The command's name, for the debug trace (see MOON_CMD_TRACE in OpenGLDriver::Execute).
		 * Static strings only: it is a pointer, not a copy. */
		const char* GetName() const { return m_name != nullptr ? m_name : "?"; }

		/** Runs this command and returns the next one; nullptr ends the batch. */
		CommandBase* Run(OpenGLDriver& p_driver)
		{
			intptr_t next = 0;
			m_execute(p_driver, this, &next);
			return reinterpret_cast<CommandBase*>(reinterpret_cast<intptr_t>(this) + next);
		}

		~CommandBase() noexcept = default;

	private:
		Execute m_execute;
		/** Names the command for the debug trace (MOON_CMD_TRACE in OpenGLDriver::Execute).
		 * A static string, never owned. */
		const char* m_name = nullptr;
	};

	/** The command for one method of OpenGLDriver. The template parameter is the method itself,
	 * so the argument types - and the call - are known without a runtime tag. */
	template <typename T>
	struct CommandType;

	template <typename... ARGS>
	struct CommandType<void (OpenGLDriver::*)(ARGS...)>
	{
		/** Arguments are stored by value, in order, right behind the command header. Only
		 * trivially copyable types belong here; anything else goes through CustomCommand. */
		using Arguments = std::tuple<std::remove_cv_t<std::remove_reference_t<ARGS>>...>;

		template <void (OpenGLDriver::*M)(ARGS...)>
		class Command final : public CommandBase
		{
		public:
			template <typename... A>
			explicit Command(const char* p_name, A&&... p_arguments) noexcept
				: CommandBase(&Command::Execute, p_name), m_arguments(std::forward<A>(p_arguments)...)
			{
			}

			// Constructed in the ring, never on the heap.
			static void* operator new(size_t, void* p_where) noexcept { return p_where; }
			static void operator delete(void*, void*) noexcept {}

			static void Execute(OpenGLDriver& p_driver, CommandBase* p_command, intptr_t* p_next) noexcept
			{
				Command* self = static_cast<Command*>(p_command);
				*p_next = static_cast<intptr_t>(CommandBase::Align(sizeof(Command)));
				Call(p_driver, self->m_arguments, std::index_sequence_for<ARGS...>{});
				// The arguments may own resources; a command lives exactly one batch.
				self->~Command();
			}

		private:
			template <size_t... I>
			static void Call(OpenGLDriver& p_driver, Arguments& p_arguments, std::index_sequence<I...>)
			{
				(p_driver.*M)(std::get<I>(p_arguments)...);
			}

			Arguments m_arguments;
		};
	};

	/** Ends a batch: the executor jumps to p_next, and nullptr stops it. This is what makes the
	 * trailing NoopCommand (or the padding of CommandStream::Allocate) part of the walk. */
	class NoopCommand final : public CommandBase
	{
	public:
		explicit NoopCommand(void* p_next) noexcept
			: CommandBase(&NoopCommand::Execute, "Noop"),
			m_next(static_cast<intptr_t>(static_cast<char*>(p_next) - reinterpret_cast<char*>(this)))
		{
		}

		static void Execute(OpenGLDriver&, CommandBase* p_command, intptr_t* p_next) noexcept
		{
			*p_next = static_cast<NoopCommand*>(p_command)->m_next;
		}

	private:
		intptr_t m_next;
	};

	/** A command that is a lambda, for the few things the driver API does not describe. It costs a
	 * heap allocation, so it is the exception, not the rule. */
	class CustomCommand final : public CommandBase
	{
	public:
		explicit CustomCommand(std::function<void()> p_command);

		static void Execute(OpenGLDriver& p_driver, CommandBase* p_command, intptr_t* p_next);

		static void* operator new(size_t, void* p_where) noexcept { return p_where; }
		static void operator delete(void*, void*) noexcept {}

	private:
		std::function<void()> m_command;
	};

	/** The recording side: it owns the recording, OpenGLDriver owns the executing.
	 *
	 * This is Filament's split, and it is what makes "a command that records a command" impossible:
	 * the concept of a command exists only here. Every method below is a real recording entry point
	 * - it reserves space in the ring and constructs the command for one OpenGLDriver method,
	 * carrying that method's pointer and its arguments. The executor never learns any of this.
	 *
	 * One of these exists per driver, and it is only ever written from the thread that records.
	 */
	class CommandStream
	{
	public:
		explicit CommandStream(CommandBufferQueue& p_queue) noexcept;

		CommandStream(const CommandStream&) = delete;
		CommandStream& operator=(const CommandStream&) = delete;

		CircularBuffer& GetBuffer() const noexcept;

		/** What to do when the ring cannot take another command. With a render thread publishing is
		 * enough (the producer blocks until the consumer frees space); single-threaded, whoever
		 * owns the stream has to run what is recorded. The stream stays free of both. */

		/** Records TCommand with p_arguments. */
		template <typename TCommand, typename... A>
		void Enqueue(A&&... p_arguments) noexcept
		{
			const size_t size = CommandBase::Align(sizeof(TCommand));
			void* const memory = m_queue.GetCircularBuffer().Allocate(size);
			new (memory) TCommand(std::forward<A>(p_arguments)...);
		}

		/** Reserves plain memory that lives until the batch has been executed. Destructors are
		 * not run - it is meant for trivially destructible data (vertex/index spans). */
		void* Allocate(size_t p_size, size_t p_alignment = 8);

		/** The frame's first command (Filament: driver.makeCurrent from FSwapChain::makeCurrent).
		 * No arguments, so it is written out rather than generated. */
		void MakeCurrent();

		/** The recording entry points: one per method of OpenGLDriver (see DriverAPI.inc). */
#define DECL_DRIVER_COMMAND(name, paramsDecl, params) void name paramsDecl;
#include <Rendering/Backend/DriverAPI.inc>
#undef DECL_DRIVER_COMMAND

	private:

		CommandBufferQueue& m_queue;
	};
}
