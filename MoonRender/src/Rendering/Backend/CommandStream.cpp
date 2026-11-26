#include <Rendering/Backend/CommandStream.h>

#include <cstdint>

#include <Rendering/Backend/CommandBufferQueue.h>
#include <Rendering/Backend/OpenGLDriver.h>
#include <Rendering/Settings/EAccessSpecifier.h>
#include <Rendering/Settings/EBufferType.h>

namespace
{

}

namespace Rendering::Backend
{
	CustomCommand::CustomCommand(std::function<void()> p_command)
		: CommandBase(&CustomCommand::Execute, "Custom"), m_command(std::move(p_command))
	{
	}

	void CustomCommand::Execute(OpenGLDriver&, CommandBase* p_command, intptr_t* p_next)
	{
		CustomCommand* self = static_cast<CustomCommand*>(p_command);
		*p_next = static_cast<intptr_t>(CommandBase::Align(sizeof(CustomCommand)));
		self->m_command();
		self->~CustomCommand();
	}

	CommandStream::CommandStream(CommandBufferQueue& p_queue) noexcept
		: m_queue(p_queue)
	{
	}

	CircularBuffer& CommandStream::GetBuffer() const noexcept
	{
		return m_queue.GetCircularBuffer();
	}





	void* CommandStream::Allocate(size_t p_size, size_t p_alignment)
	{
		// Pad the request so a NoopCommand fits in front of it and the payload can still be
		// aligned: the Noop then jumps over the whole block, which is how arbitrary memory and
		// commands can share the ring.
		const size_t size = CommandBase::Align(sizeof(NoopCommand) + p_size + p_alignment - 1);
		char* const blockStart = static_cast<char*>(m_queue.GetCircularBuffer().Allocate(size));
		new (blockStart) NoopCommand(blockStart + size);

		const uintptr_t alignment = static_cast<uintptr_t>(p_alignment);
		const uintptr_t payload = (reinterpret_cast<uintptr_t>(blockStart) + sizeof(NoopCommand) + alignment - 1)
			& ~(alignment - 1);
		return reinterpret_cast<void*>(payload);
	}

	// Hand-written because it takes no arguments: the generated form expands to a trailing comma
	// (Filament's macro system has a no-argument variant, ours does not). Same thing otherwise -
	// the command carries the executor's method pointer.
	void CommandStream::MakeCurrent()
	{
		using Command = CommandType<decltype(&OpenGLDriver::MakeCurrent)>::template Command<&OpenGLDriver::MakeCurrent>;
		Enqueue<Command>("MakeCurrent");
	}

	// The recording entry points. Each of these is what the main thread calls: it reserves the
	// command in the ring and constructs it with the executor's method pointer. The body of the
	// method - what actually reaches GL - is on OpenGLDriver and knows nothing about any of this.
#define MOON_ENQUEUE_COMMAND(name, exec, ...) \
	{ \
		using Command = CommandType< \
			decltype(&::Rendering::Backend::OpenGLDriver::exec)>::template Command< \
				&::Rendering::Backend::OpenGLDriver::exec>; \
		Enqueue<Command>(#name, __VA_ARGS__); \
	}

#define DECL_DRIVER_COMMAND(name, paramsDecl, params) \
	void CommandStream::name paramsDecl \
	{ \
		MOON_ENQUEUE_COMMAND(name, name, MOON_EXPAND_ARGS params) \
	}

/* Filament's driver API: the recording method keeps the driver method's name, and its body is the
 * command - nothing else. */
#define DECL_DRIVER_API(name, paramsDecl, params) \
	void CommandStream::name paramsDecl \
	{ \
		MOON_ENQUEUE_COMMAND(name, name, MOON_EXPAND_ARGS params) \
	}

/* The call that hands a value back: the handle the driver reserved travels as the command's first
 * argument, ahead of the caller's own (createBufferObjectS / createBufferObjectR). */
#define DECL_DRIVER_API_RETURN(RetType, name, paramsDecl, params) \
	void CommandStream::name(RetType p_handle, MOON_EXPAND_ARGS paramsDecl) \
	{ \
		MOON_ENQUEUE_COMMAND(name, name, p_handle, MOON_EXPAND_ARGS params) \
	}

#include <Rendering/Backend/DriverAPI.inc>
#undef DECL_DRIVER_COMMAND
#undef DECL_DRIVER_API
#undef DECL_DRIVER_API_RETURN
#undef MOON_ENQUEUE_COMMAND
}
