#include <Rendering/Backend/OpenGLDriver.h>
#include <algorithm>
#include <string>
#include <vector>
#include <Rendering/HAL/OpenGL/GLProgramBindings.h>

#include <glad/glad.h>


#include <Rendering/Backend/CommandStream.h>

#include <cstdio>
#include <cstdlib>
#include <Rendering/Backend/BackendResources.h>
#include <Rendering/HAL/ShaderProgram.h>
#include <Rendering/HAL/Framebuffer.h>
#include <Rendering/HAL/Renderbuffer.h>
#include <Rendering/HAL/VertexArray.h>
#include <Rendering/Backend/BindFramebufferRequest.h>
#include <Rendering/Backend/FramebufferRequests.h>
#include <Rendering/Backend/TextureRequests.h>
#include <Core/Rendering/FramebufferUtil.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>
#include <Rendering/HAL/OpenGL/GLBuffer.h>
#include <Rendering/Data/DescriptorSet.h>
#include <Rendering/Settings/EBindingPoint.h>
#include <Rendering/Utils/Conversions.h>
#include <Rendering/Utils/FrameTrace.h>

#include <cstdint>

#include <tracy/Tracy.hpp>

namespace
{
	/** The buffer object a handle names, or null when there is none: the create command that makes
	 * it runs earlier in the same batch, and a released or stale handle resolves to nothing
	 * (Filament's handle_cast<GLBufferObject*>). */
	::Rendering::HAL::GLBuffer* ResolveBuffer(uint32_t p_handleId)
	{
		return ::Rendering::Backend::GetHandleAllocator().HandleCast<::Rendering::HAL::GLBuffer>(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject>{ p_handleId });
	}

	/** The same for the container objects: the render target and its renderbuffer. */
	::Rendering::HAL::Framebuffer* ResolveFramebuffer(uint32_t p_handleId)
	{
		return ::Rendering::Backend::GetHandleAllocator().HandleCast<::Rendering::HAL::Framebuffer>(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer>{ p_handleId });
	}

	::Rendering::HAL::Renderbuffer* ResolveRenderbuffer(uint32_t p_handleId)
	{
		return ::Rendering::Backend::GetHandleAllocator().HandleCast<::Rendering::HAL::Renderbuffer>(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwRenderbuffer>{ p_handleId });
	}
}
namespace Rendering::Backend
{
	void OpenGLDriver::Execute(void* p_begin, void* p_end)
	{
		if (p_begin == nullptr || p_begin == p_end)
		{
			return;
		}

		// MOON_CMD_TRACE: name the command that raised a GL error. The error queue is per context,
		// so one command's failure surfaces at whatever the next check is; this attributes it to the
		// command that caused it instead of to the draw that happened to look next.
		static const bool traceCommands = std::getenv("MOON_CMD_TRACE") != nullptr;

		const intptr_t end = reinterpret_cast<intptr_t>(p_end);
		CommandBase* command = static_cast<CommandBase*>(p_begin);
		while (command != nullptr && reinterpret_cast<intptr_t>(command) < end)
		{
			CommandBase* const current = command;
			command = current->Run(*this);
			if (traceCommands)
			{
				const GLenum error = glGetError();
				if (error != GL_NO_ERROR)
				{
					std::fprintf(stderr, "[cmd] %s -> 0x%04x\n", current->GetName(),
						static_cast<unsigned>(error));
					std::fflush(stderr);
				}
			}
		}
	}

	void OpenGLDriver::ResetAppliedState(const ::Rendering::Data::PipelineState& p_state)
	{
		m_appliedState = p_state;
	}

	// One body per entry of DriverAPI.inc. They are listed here by hand rather than generated:
	// a body is where the conversion to the backend's types happens, and that is worth reading.

	void OpenGLDriver::Viewport(int32_t p_x, int32_t p_y, int32_t p_width, int32_t p_height)
	{
		m_backend.SetViewport(
			static_cast<uint32_t>(p_x),
			static_cast<uint32_t>(p_y),
			static_cast<uint32_t>(p_width),
			static_cast<uint32_t>(p_height));
	}

	void OpenGLDriver::Scissor(int32_t p_x, int32_t p_y, int32_t p_width, int32_t p_height)
	{
		m_backend.SetScissor(
			static_cast<uint32_t>(p_x),
			static_cast<uint32_t>(p_y),
			static_cast<uint32_t>(p_width),
			static_cast<uint32_t>(p_height));
	}

	void OpenGLDriver::Capability(::Rendering::Settings::ERenderingCapability p_capability, bool p_enabled)
	{
		m_backend.SetCapability(p_capability, p_enabled);

		// The pipeline state is this side's cache of what GL is in, so a capability flipped outside
		// it has to be reflected here too: otherwise the next PipelineState that asks for the
		// opposite of the *cached* value would not issue the call (the overlay leaves SCISSOR_TEST
		// on, and the next frame's state says off - the diff used to miss exactly that).
		using namespace ::Rendering::Settings;
		switch (p_capability)
		{
		case ERenderingCapability::BLEND: m_appliedState.blending = p_enabled; break;
		case ERenderingCapability::CULL_FACE: m_appliedState.culling = p_enabled; break;
		case ERenderingCapability::DEPTH_TEST: m_appliedState.depthTest = p_enabled; break;
		case ERenderingCapability::DITHER: m_appliedState.dither = p_enabled; break;
		case ERenderingCapability::POLYGON_OFFSET_FILL: m_appliedState.polygonOffsetFill = p_enabled; break;
		case ERenderingCapability::SAMPLE_ALPHA_TO_COVERAGE: m_appliedState.sampleAlphaToCoverage = p_enabled; break;
		case ERenderingCapability::SCISSOR_TEST: m_appliedState.scissorTest = p_enabled; break;
		case ERenderingCapability::STENCIL_TEST: m_appliedState.stencilTest = p_enabled; break;
		case ERenderingCapability::MULTISAMPLE: m_appliedState.multisample = p_enabled; break;
		default: break;
		}
	}

	void OpenGLDriver::BlendingEquation(::Rendering::Settings::EBlendingEquation p_equation)
	{
		m_backend.SetBlendingEquation(p_equation);
	}

	void OpenGLDriver::BlendingFunction(
		::Rendering::Settings::EBlendingFactor p_source,
		::Rendering::Settings::EBlendingFactor p_destination)
	{
		m_backend.SetBlendingFunction(p_source, p_destination);
	}

	/** Binds the pass's render target; null means the default framebuffer. The name is asked for
	 * here, when the command runs, so a target resized after recording still binds right. */
	void OpenGLDriver::BindFramebuffer(const ::Rendering::Backend::BindFramebufferRequest& p_request)
	{
		const uint32_t id = p_request.target != nullptr ? p_request.target->GetID() : 0;
		m_backend.BindFramebuffer(id);

		// MOON_FRAME_TRACE: which target is bound and whether it is complete. A pass that draws
		// into a target whose attachments were never built reports here, with the GL status, instead
		// of a stream of GL_INVALID_FRAMEBUFFER_OPERATION later. The pass name is the label the
		// recording side published, because this side only sees the bind.
		if (std::getenv("MOON_FRAME_TRACE") != nullptr && id != 0)
		{
			const GLenum status = glCheckNamedFramebufferStatus(id, GL_FRAMEBUFFER);
			const auto size = p_request.target->GetSize();
			std::fprintf(stderr, "[fbo] bind %u (%s) %ux%u status=0x%04x pass=%s\n", id,
				p_request.target->GetDebugName().c_str(), size.first, size.second, status,
				::Rendering::Utils::FrameTrace::GetPass());
			std::fflush(stderr);
		}
	}

	/** Binds the mesh's array, draws it, unbinds. The array name is asked for here, at replay
	 * time: a mesh whose upload was deferred to this side (see Backend::DeferredDeleter) has one
	 * by now, and no GL name is ever cached on the recording side. */
	void OpenGLDriver::UpdateTexture(::Rendering::Backend::UpdateTextureRequest p_request)
	{
		const auto handle = ::Rendering::Backend::ResourceHandle<Rendering::HAL::Texture>{ p_request.handleId };
		auto* target = ::Rendering::Backend::GetHandleAllocator().HandleCast<Rendering::HAL::Texture>(handle);
		if (target == nullptr)
		{
			return;
		}

		target->UploadSubImage(p_request.pixels.data(), p_request.format, p_request.type,
			p_request.x, p_request.y, p_request.width, p_request.height);
	}



	void OpenGLDriver::MakeCurrent()
	{
		// Filament's OpenGLDriver::makeCurrent: the platform makes the context current, then the
		// bindings that belonged to whatever context was current before are invalidated. Our
		// per-context state is re-applied by the frame's own commands.
		if (m_contextHost == nullptr)
		{
			return;
		}
		if (!m_contextHost->MakeCurrent())
		{
			static bool reported = false;
			if (!reported)
			{
				reported = true;
				std::fprintf(stderr, "[GL] makeCurrent failed on the executor's thread\n");
				std::fflush(stderr);
			}
		}
	}

	void OpenGLDriver::CreateProgram(::Rendering::Backend::CreateProgramRequest p_request)
	{
		// Filament's createProgram: the stages' sources travel with the command and the build
		// happens here, where the context is. The program object lives in the handle's slot - the
		// construct below is what makes it - and its Build is what attaches, links, reflects and
		// throws the stages away. The binding map is declared right after the link, because that is
		// when the program exists to be told about its slots.
		if (p_request.programHandleId == 0 || p_request.stages == nullptr)
		{
			return;
		}

		using namespace ::Rendering::Backend;
		const ResourceHandle<HwProgram> handle{ p_request.programHandleId };

		Records::CreateProgram(handle);
		Rendering::HAL::ShaderProgram* const program =
			GetHandleAllocator().HandleCast<Rendering::HAL::ShaderProgram>(handle);
		if (program == nullptr)
		{
			return;
		}

		program->Build(*p_request.stages);

		if (p_request.bindings != nullptr)
		{
			program->DeclareBindings(*p_request.bindings);
		}
	}

	void OpenGLDriver::BindProgram(uint32_t p_programId)
	{
		// Filament's bindProgram: the actual call, with the program the create command produced.
		glUseProgram(p_programId);
	}

	void OpenGLDriver::BindProgramHandle(uint32_t p_programHandleId)
	{
		// The object only exists after the create command ran - which is exactly why this variant
		// exists: the recording side records the handle, this side resolves it. A null here means
		// the program is not built (yet): leave the bound one alone rather than unbinding
		// everything.
		::Rendering::HAL::ShaderProgram* const program =
			::Rendering::Backend::GetHandleAllocator().HandleCast<::Rendering::HAL::ShaderProgram>(
				::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwProgram>{
					p_programHandleId });
		if (program != nullptr)
		{
			program->Bind();
		}
	}

	void OpenGLDriver::DeclareProgramBindings(::Rendering::Backend::DeclareProgramBindingsRequest p_request)
	{
		::Rendering::HAL::ShaderProgram* const program =
			::Rendering::Backend::GetHandleAllocator().HandleCast<::Rendering::HAL::ShaderProgram>(
				::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwProgram>{
					p_request.programHandleId });
		if (program == nullptr || p_request.bindings == nullptr)
		{
			return;
		}

		program->DeclareBindings(*p_request.bindings);
	}

	void OpenGLDriver::ReadPixels(const ::Rendering::Backend::ReadPixelsRequest& p_request)
	{
		// A handle is resolved here, so a target created in this batch can be read (Filament's
		// readPixels takes the target the same way); a raw name is what the callers that already
		// have one pass.
		uint32_t framebuffer = p_request.framebufferId;
		if (p_request.framebufferHandleId != 0)
		{
			framebuffer = ::Rendering::Backend::Records::FramebufferName(
				::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer>{
					p_request.framebufferHandleId });
		}

		if (framebuffer == 0 || p_request.data == nullptr || p_request.width == 0 || p_request.height == 0)
		{
			return;
		}

		// Filament's readPixels: bind the target, select the attachment, read. The bytes go to the
		// buffer the request owns, and the completion callback runs here - on the thread that owns
		// the context - so a caller on the recording side never reads a buffer the GPU is filling.
		glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
		if (p_request.readBufferIndex >= 0)
		{
			// A negative index keeps whatever the recorded SetFramebufferReadBuffer selected.
			glReadBuffer(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(p_request.readBufferIndex));
		}
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(static_cast<GLint>(p_request.x), static_cast<GLint>(p_request.y),
			static_cast<GLsizei>(p_request.width), static_cast<GLsizei>(p_request.height),
			::Rendering::HAL::EnumToValue<GLenum>(p_request.format),
			::Rendering::HAL::EnumToValue<GLenum>(p_request.type),
			p_request.data->data());
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

		if (p_request.onDone)
		{
			p_request.onDone();
		}
	}

	/** glNamedFramebufferReadBuffer on the target the handle names: the first half of a recorded
	 * readback pair (picking selects the id attachment, reads it, then the position one). */
	void OpenGLDriver::SetFramebufferReadBuffer(const ::Rendering::Backend::SetFramebufferReadBufferRequest& p_request)
	{
		::Rendering::HAL::Framebuffer* const framebuffer = ResolveFramebuffer(p_request.framebufferHandleId);
		if (framebuffer == nullptr)
		{
			return;
		}
		framebuffer->SetReadBuffer(p_request.hasIndex
			? std::optional<uint32_t>(p_request.index)
			: std::nullopt);
	}

	/** glReadPixels into a pixel-pack buffer: the GPU writes it, and DownloadBuffer fetches it once
	 * the buffer's fence says it is done. */
	void OpenGLDriver::ReadPixelsToBuffer(::Rendering::Backend::ReadPixelsToBufferRequest p_request)
	{
		::Rendering::HAL::Framebuffer* const framebuffer = ResolveFramebuffer(p_request.framebufferHandleId);
		const uint32_t buffer = ::Rendering::Backend::Records::BufferName(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject>{
				p_request.bufferHandleId });
		if (framebuffer == nullptr || buffer == 0)
		{
			return;
		}

		framebuffer->ReadPixelsToBuffer(buffer, p_request.offset, p_request.x, p_request.y,
			p_request.width, p_request.height, p_request.format, p_request.type);
	}

	/** The fetch half of the asynchronous readback: copies the buffer range into the request's CPU
	 * memory, but only once the fence says the GPU is done with it. Telling the recording side that
	 * it has to try again is the callback's false. */
	void OpenGLDriver::DownloadBuffer(::Rendering::Backend::DownloadBufferRequest p_request)
	{
		::Rendering::HAL::GLBuffer* const buffer = ResolveBuffer(p_request.handleId);
		if (buffer == nullptr || p_request.data == nullptr || p_request.size == 0)
		{
			return;
		}

		if (!buffer->IsFenceSignaled())
		{
			if (p_request.onDone)
			{
				p_request.onDone(false);
			}
			return;
		}

		if (p_request.data->size() < p_request.size)
		{
			p_request.data->resize(static_cast<size_t>(p_request.size));
		}

		// glGetNamedBufferSubData with the fence already signalled: a copy, not a stall.
		glGetNamedBufferSubData(buffer->GetID(), static_cast<GLintptr>(p_request.offset),
			static_cast<GLsizeiptr>(p_request.size), p_request.data->data());

		if (p_request.onDone)
		{
			p_request.onDone(true);
		}
	}

	void OpenGLDriver::SetVertexArrayLayout(const ::Rendering::Backend::SetVertexArrayLayoutRequest& p_request)
	{
		// The array and its buffers are resolved here: the create commands that made their GL names
		// ran earlier in this batch (Filament's setVertexArrayObject).
		::Rendering::HAL::VertexArray* const array =
			::Rendering::Backend::GetHandleAllocator().HandleCast<::Rendering::HAL::VertexArray>(
				::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwVertexArray>{
					p_request.vertexArrayHandleId });
		::Rendering::HAL::GLBuffer* const vertexBuffer = ResolveBuffer(p_request.vertexBufferHandleId);
		if (array == nullptr || vertexBuffer == nullptr || p_request.attributes == nullptr)
		{
			return;
		}
		::Rendering::HAL::GLBuffer* const indexBuffer = ResolveBuffer(p_request.indexBufferHandleId);

		array->SetLayout(*p_request.attributes, vertexBuffer->GetID(),
			indexBuffer != nullptr ? indexBuffer->GetID() : 0u);
	}

	void OpenGLDriver::UpdateBuffer(const ::Rendering::Backend::UpdateBufferRequest& p_request)
	{
		if (p_request.handleId == 0 || p_request.data == nullptr || p_request.size == 0)
		{
			return;
		}
		// The object belongs to the create command, which ran earlier in this batch: the handle is
		// resolved on this side, because that is where the object (and its GL name) lives.
		::Rendering::HAL::GLBuffer* const buffer = ResolveBuffer(p_request.handleId);
		if (buffer == nullptr)
		{
			return;
		}
		buffer->Upload(p_request.data->data(),
			::Rendering::HAL::BufferMemoryRange{ p_request.offset, p_request.size });
	}

	void OpenGLDriver::BindDescriptor(const ::Rendering::Backend::BindDescriptorRequest& p_request)
	{
		using Kind = ::Rendering::Backend::BindDescriptorRequest::Kind;

		// A handle names the front end's object, whose GL name only exists once its create command
		// has run; a backend-only resource (render-target attachment, engine fallback) has no
		// handle and carries its name directly.
		const auto resolveBuffer = [&p_request]() -> uint32_t
		{
			if (p_request.handleId != 0)
			{
				return ::Rendering::Backend::Records::BufferName(
					::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject>{
						p_request.handleId });
			}
			return p_request.objectId;
		};
		const auto resolveTexture = [&p_request]() -> uint32_t
		{
			if (p_request.handleId != 0)
			{
				return ::Rendering::Backend::Records::TextureName(p_request.handleId);
			}
			return p_request.objectId;
		};

		switch (p_request.kind)
		{
		case Kind::UniformBuffer:
		{
			const uint32_t name = resolveBuffer();
			if (name != 0)
			{
				glBindBufferRange(GL_UNIFORM_BUFFER, p_request.slot, name,
					p_request.offset, p_request.size);
			}
			break;
		}
		case Kind::ShaderStorageBuffer:
		{
			const uint32_t name = resolveBuffer();
			if (name != 0)
			{
				glBindBufferRange(GL_SHADER_STORAGE_BUFFER, p_request.slot, name,
					p_request.offset, p_request.size);
			}
			break;
		}
		case Kind::Texture:
		{
			const uint32_t name = resolveTexture();
			if (name != 0)
			{
				glBindTextureUnit(p_request.slot, name);
			}
			break;
		}
		}
	}

	void OpenGLDriver::SetupFramebuffer(const ::Rendering::Backend::SetupFramebufferRequest& p_request)
	{
		if (std::getenv("MOON_FRAME_TRACE") != nullptr)
		{
			std::fprintf(stderr, "[setup] execute target=%p name=%s desc=%d\n",
				static_cast<void*>(p_request.target),
				p_request.target != nullptr ? p_request.target->GetDebugName().c_str() : "-",
				p_request.useDesc ? 1 : 0);
			std::fflush(stderr);
		}

		if (p_request.target == nullptr)
		{
			return;
		}

		// The whole setup - the colour texture, the depth/stencil renderbuffer, the attachments -
		// is GL, so it happens here (Filament's createRenderTargetR).
		if (p_request.useDesc)
		{
			::Core::Rendering::FramebufferUtil::SetupFramebuffer(
				*p_request.target,
				p_request.desc,
				p_request.useDepth,
				p_request.useStencil,
				p_request.useMultiSample);
		}
		else
		{
			::Core::Rendering::FramebufferUtil::SetupFramebuffer(
				*p_request.target,
				p_request.width,
				p_request.height,
				p_request.useDepth,
				p_request.useStencil,
				p_request.useMipMaps,
				p_request.useMultiSample);
		}
	}

	void OpenGLDriver::ResizeFramebuffer(const ::Rendering::Backend::ResizeFramebufferRequest& p_request)
	{
		if (p_request.target != nullptr)
		{
			p_request.target->Resize(p_request.width, p_request.height);
		}
	}

	void OpenGLDriver::CopyFramebufferColor(const ::Rendering::Backend::CopyFramebufferColorRequest& p_request)
	{
		if (p_request.source != nullptr && p_request.target != nullptr)
		{
			::Core::Rendering::FramebufferUtil::CopyFramebufferColor(
				*p_request.source, static_cast<int>(p_request.sourceIndex),
				*p_request.target, static_cast<int>(p_request.targetIndex));
		}
	}

	void OpenGLDriver::CopyFramebufferDepth(const ::Rendering::Backend::CopyFramebufferDepthRequest& p_request)
	{
		if (p_request.source != nullptr && p_request.target != nullptr)
		{
			::Core::Rendering::FramebufferUtil::CopyFramebufferDepth(
				*p_request.source, *p_request.target);
		}
	}

	void OpenGLDriver::AttachRenderbuffer(const ::Rendering::Backend::AttachRenderbufferRequest& p_request)
	{
		::Rendering::HAL::Framebuffer* const framebuffer =
			ResolveFramebuffer(p_request.framebufferHandleId);
		::Rendering::HAL::Renderbuffer* const renderbuffer =
			ResolveRenderbuffer(p_request.renderbufferHandleId);
		if (framebuffer == nullptr || renderbuffer == nullptr)
		{
			return;
		}

		// The bind is kept because the immediate path has it (and some drivers want the
		// renderbuffer current before it is attached).
		renderbuffer->Bind();
		framebuffer->AttachRenderbuffer(p_request.attachment, p_request.index, renderbuffer->GetID());
	}

	void OpenGLDriver::AllocateRenderbuffer(const ::Rendering::Backend::AllocateRenderbufferRequest& p_request)
	{
		::Rendering::HAL::Renderbuffer* const renderbuffer = ResolveRenderbuffer(p_request.handleId);
		if (renderbuffer == nullptr)
		{
			return;
		}
		renderbuffer->Allocate(p_request.width, p_request.height, p_request.format,
			p_request.multiSample);
	}

	void OpenGLDriver::AttachTexture(const ::Rendering::Backend::AttachTextureRequest& p_request)
	{
		::Rendering::HAL::Framebuffer* const framebuffer =
			ResolveFramebuffer(p_request.framebufferHandleId);
		const uint32_t texture = ::Rendering::Backend::Records::TextureName(p_request.textureHandleId);
		if (framebuffer == nullptr || texture == 0)
		{
			return;
		}

		if (p_request.useLayer)
		{
			framebuffer->AttachTextureLayer(p_request.attachment, p_request.index, texture,
				p_request.level, p_request.layer);
		}
		else
		{
			framebuffer->AttachTexture(p_request.attachment, p_request.index, texture,
				p_request.level);
		}
	}

	void OpenGLDriver::SetFramebufferDrawBuffers(const ::Rendering::Backend::SetFramebufferDrawBuffersRequest& p_request)
	{
		::Rendering::HAL::Framebuffer* const framebuffer =
			ResolveFramebuffer(p_request.framebufferHandleId);
		if (framebuffer == nullptr)
		{
			return;
		}
		framebuffer->SetDrawBuffers(p_request.indices);
	}

	void OpenGLDriver::CreateFramebuffer(uint32_t p_handleId)
	{
		::Rendering::Backend::Records::CreateFramebuffer(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer>{ p_handleId });
	}

	void OpenGLDriver::CreateRenderbuffer(uint32_t p_handleId)
	{
		::Rendering::Backend::Records::CreateRenderbuffer(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwRenderbuffer>{ p_handleId });
	}

	void OpenGLDriver::CreateVertexArray(uint32_t p_handleId)
	{
		::Rendering::Backend::Records::CreateVertexArray(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwVertexArray>{ p_handleId });
	}

	void OpenGLDriver::AllocateBuffer(::Rendering::Backend::AllocateBufferRequest p_request)
	{
		// The storage belongs to the object the create command made earlier in this batch
		// (Filament's allocateBufferObject): it is where the size the executor just allocated is
		// recorded, so anything resolving the handle afterwards can read it.
		::Rendering::HAL::GLBuffer* const buffer = ResolveBuffer(p_request.handleId);
		if (buffer == nullptr)
		{
			return;
		}
		buffer->Allocate(p_request.size, p_request.usage);
	}

	void OpenGLDriver::CreateBuffer(::Rendering::Backend::CreateBufferRequest p_request)
	{
		::Rendering::Backend::Records::CreateBuffer(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject>{ p_request.handleId },
			p_request.type);
	}

	/** Sets a buffer's binding point where the context lives. A shader storage buffer bound on the
	 * recording side is simply not bound when the draws run on another thread: the light list then
	 * reads as empty and every lit surface comes out darker (docs §8.67). */
	void OpenGLDriver::BindBuffer(const ::Rendering::Backend::BindBufferRequest& p_request)
	{
		if (p_request.handleId == 0)
		{
			glBindBuffer(::Rendering::HAL::EnumToValue<GLenum>(p_request.type), 0);
			return;
		}

		::Rendering::HAL::GLBuffer* const buffer = ResolveBuffer(p_request.handleId);
		if (buffer == nullptr)
		{
			return;
		}

		// The object knows its own target, which is why the create command had to carry the kind
		// of buffer: binding a uniform/SSBO buffer as GL_ARRAY_BUFFER is an error.
		if (p_request.indexed)
		{
			buffer->Bind(p_request.index);
		}
		else
		{
			buffer->Bind();
		}
	}

	/** glBindBufferRange: a block inside a shared buffer (the pool's slices, a descriptor that
	 * carries an offset + size instead of a whole buffer). */
	void OpenGLDriver::BindBufferRange(const ::Rendering::Backend::BindBufferRangeRequest& p_request)
	{
		::Rendering::HAL::GLBuffer* const buffer = ResolveBuffer(p_request.handleId);
		if (buffer == nullptr)
		{
			return;
		}
		buffer->BindRange(p_request.index, p_request.offset, p_request.size);
	}

	void OpenGLDriver::UnmapBuffer(uint32_t p_handleId)
	{
		::Rendering::HAL::GLBuffer* const buffer = ResolveBuffer(p_handleId);
		if (buffer == nullptr)
		{
			return;
		}
		buffer->Unmap();
	}

	void OpenGLDriver::InsertFence(uint32_t p_handleId)
	{
		::Rendering::HAL::GLBuffer* const buffer = ResolveBuffer(p_handleId);
		if (buffer == nullptr)
		{
			return;
		}
		buffer->InsertFence();
	}

	void OpenGLDriver::ClearFence(uint32_t p_handleId)
	{
		::Rendering::HAL::GLBuffer* const buffer = ResolveBuffer(p_handleId);
		if (buffer == nullptr)
		{
			return;
		}
		buffer->ClearFence();
	}



	void OpenGLDriver::CreateTexture(::Rendering::Backend::CreateTextureRequest p_request)
	{
		const auto handle = ::Rendering::Backend::ResourceHandle<Rendering::HAL::Texture>{ p_request.handleId };
		auto* target = ::Rendering::Backend::Records::CreateTexture(handle, p_request.type, p_request.debugName);
		if (target == nullptr)
		{
			return;
		}

		if (p_request.allocate)
		{
			auto descriptor = p_request.descriptor;
			if (descriptor.mutableDesc.has_value() && !p_request.pixels.empty())
			{
				// The descriptor names the recording side's vector as its data (the sketch's block
				// colours, a texture buffer): by the time this runs that memory may have been
				// rewritten - which is what made the solid's colours differ between runs. The bytes
				// that travel with the command are the ones to allocate from.
				descriptor.mutableDesc.value().data = p_request.pixels.data();
			}

			// A texture's storage is immutable (glTextureStorage2D): allocating again is
			// GL_INVALID_OPERATION "Texture is immutable". Only a mutable texture - a texture buffer
			// is one, its data buffer is rebuilt on every submit - may be re-allocated in place.
			if (!target->IsValid() || descriptor.mutableDesc.has_value())
			{
				target->Allocate(descriptor);
			}
		}
	}

	void OpenGLDriver::UploadTexture(::Rendering::Backend::TextureUploadRequest p_request)
	{
		const auto handle = ::Rendering::Backend::ResourceHandle<Rendering::HAL::Texture>{
			p_request.handleId };
		auto* target = ::Rendering::Backend::GetHandleAllocator().HandleCast<Rendering::HAL::Texture>(handle);
		if (target == nullptr || p_request.pixels.empty())
		{
			return;
		}

		// Storage once: a texture's storage is immutable (glTextureStorage2D), so allocating again
		// answers GL_INVALID_OPERATION "Texture is immutable". A re-upload - the font atlas grows as
		// glyphs are baked - only needs the sub-image, which is what Upload does. IsValid() is the
		// allocated flag the descriptor sets.
		if (!target->IsValid())
		{
			target->Allocate(p_request.descriptor);
		}
		target->Upload(p_request.pixels.data(), p_request.format, p_request.type);
		if (p_request.generateMipmaps)
		{
			target->GenerateMipmaps();
		}
	}

	/** Reads a texture back and prints the first bytes: the diagnostic for "is the object holding
	 * what the recording side uploaded" (a texture buffer's contents cannot be seen from a draw). */
	void OpenGLDriver::PrintTextureBytes(uint32_t p_textureHandleId, uint32_t p_byteCount, const char* p_label)
	{
		const auto handle = ::Rendering::Backend::ResourceHandle<::Rendering::HAL::Texture>{ p_textureHandleId };
		const auto* target = ::Rendering::Backend::GetHandleAllocator().HandleCast<::Rendering::HAL::Texture>(handle);
		if (target == nullptr || target->GetID() == 0 || p_byteCount == 0)
		{
			std::fprintf(stderr, "[texbytes] %s missing\n", p_label != nullptr ? p_label : "?");
			std::fflush(stderr);
			return;
		}

		std::vector<uint8_t> bytes(p_byteCount, 0);
		glGetTextureImage(target->GetID(), 0, GL_RGBA, GL_FLOAT, static_cast<GLsizei>(bytes.size()), bytes.data());
		std::fprintf(stderr, "[texbytes] %s id=%u bytes=", p_label != nullptr ? p_label : "?", target->GetID());
		for (uint32_t i = 0; i < p_byteCount && i < 16; ++i) { std::fprintf(stderr, "%02x", bytes[i]); }
		std::fprintf(stderr, "\n");
		std::fflush(stderr);
	}

	void OpenGLDriver::SetTextureBorderColor(const ::Rendering::Backend::SetTextureBorderColorRequest& p_request)
	{
		const auto handle = ::Rendering::Backend::ResourceHandle<Rendering::HAL::Texture>{ p_request.handleId };
		auto* target = ::Rendering::Backend::GetHandleAllocator().HandleCast<Rendering::HAL::Texture>(handle);
		if (target != nullptr)
		{
			target->SetBorderColor(p_request.color);
		}
	}

	void OpenGLDriver::GenerateTextureMipmaps(const ::Rendering::Backend::GenerateTextureMipmapsRequest& p_request)
	{
		const auto handle = ::Rendering::Backend::ResourceHandle<Rendering::HAL::Texture>{ p_request.handleId };
		auto* target = ::Rendering::Backend::GetHandleAllocator().HandleCast<Rendering::HAL::Texture>(handle);
		if (target != nullptr)
		{
			target->GenerateMipmaps();
		}
	}

	void OpenGLDriver::DrawVertexArray(const ::Rendering::Backend::DrawVertexArrayRequest& p_request)
	{
		const uint32_t array = ::Rendering::Backend::Records::VertexArrayName(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwVertexArray>{
				p_request.vertexArrayHandleId });
		if (array == 0 || p_request.vertexCount == 0)
		{
			return;
		}

		m_backend.BindVertexArray(array);
		if (p_request.viewportWidth > 0 && p_request.viewportHeight > 0)
		{
			m_backend.SetViewport(
				static_cast<uint32_t>(std::max(0, p_request.viewportX)),
				static_cast<uint32_t>(std::max(0, p_request.viewportY)),
				static_cast<uint32_t>(p_request.viewportWidth),
				static_cast<uint32_t>(p_request.viewportHeight));
		}
		if (std::getenv("MOON_FRAME_TRACE") != nullptr)
		{
			GLint target = 0;
			GLint program = 0;
			GLint viewport[4] = { 0, 0, 0, 0 };
			glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
			glGetIntegerv(GL_CURRENT_PROGRAM, &program);
			glGetIntegerv(GL_VIEWPORT, viewport);
			std::fprintf(stderr,
				"[ova] pass=%s array=%u count=%u prim=0x%04x target=%d program=%u vp=%d,%d,%dx%d\n",
				::Rendering::Utils::FrameTrace::GetPass(), array, p_request.vertexCount,
				p_request.primitiveMode, static_cast<int>(target), program,
				viewport[0], viewport[1], viewport[2], viewport[3]);
			std::fflush(stderr);
		}
		glDrawArrays(static_cast<GLenum>(p_request.primitiveMode), 0,
			static_cast<GLsizei>(p_request.vertexCount));
	}

	/** One entry of the 2D overlay's list: bind its array, scissor, bind its texture, draw the range. */
	void OpenGLDriver::DrawIndexedVertexArray(const ::Rendering::Backend::DrawIndexedVertexArrayRequest& p_request)
	{
		using namespace ::Rendering::Backend;

		const uint32_t array = Records::VertexArrayName(
			ResourceHandle<HwVertexArray>{ p_request.vertexArrayHandleId });
		if (array == 0 || p_request.indexCount == 0)
		{
			return;
		}

		if (p_request.scissor)
		{
			// The list clips in pixels from the top-left; glScissor counts from the bottom-left,
			// which is the flip the recording side already did before handing the rectangle over.
			glEnable(GL_SCISSOR_TEST);
			glScissor(p_request.scissorX, p_request.scissorY,
				p_request.scissorWidth, p_request.scissorHeight);
		}

		uint32_t texture = p_request.textureName;
		if (p_request.textureHandleId != 0)
		{
			// A front-end texture: its GL name only exists on this side, and only after the create
			// command that ran earlier in the batch.
			const auto handle = ResourceHandle<::Rendering::HAL::Texture>{ p_request.textureHandleId };
			if (const auto* resolved = GetHandleAllocator().HandleCast<::Rendering::HAL::Texture>(handle))
			{
				texture = resolved->GetID();
			}
		}

		m_backend.BindVertexArray(array);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture);
		// Absolute vertex indices across the whole buffer: the list writes them that way, so the
		// command only selects a range of them (no base vertex).
		glDrawElements(static_cast<GLenum>(p_request.primitiveMode),
			static_cast<GLsizei>(p_request.indexCount), GL_UNSIGNED_INT,
			reinterpret_cast<void*>(static_cast<uintptr_t>(p_request.indexOffset) * sizeof(uint32_t)));
	}

	void OpenGLDriver::SetProgramMatrix4(const ::Rendering::Backend::ProgramMatrix4Request& p_request)
	{
		if (p_request.programName == 0)
		{
			return;
		}

		// glUniform* applies to the program in use.
		glUseProgram(p_request.programName);
		const GLint location = glGetUniformLocation(p_request.programName, p_request.name.c_str());
		if (location >= 0)
		{
			glUniformMatrix4fv(location, 1, GL_FALSE, p_request.value.data());
		}
	}

	void OpenGLDriver::UploadOverlayVertices(::Rendering::Backend::UploadOverlayVerticesRequest p_request)
	{
		using namespace ::Rendering::Backend;

		const uint32_t array = Records::VertexArrayName(
			ResourceHandle<HwVertexArray>{ p_request.vertexArrayHandleId });
		const uint32_t buffer = Records::BufferName(
			ResourceHandle<HwBufferObject>{ p_request.vertexBufferHandleId });
		if (array == 0 || buffer == 0 || p_request.stride == 0 || p_request.vertices.empty())
		{
			return;
		}

		// The overlay's VertexFormat (editor side): position+size at 0, colour at 16, normal at 20,
		// uv at 32. The stride comes with the request because that struct is padded, and the
		// pointers are re-described on every upload because the stride changes between draws.
		constexpr GLuint kPositionLocation = 0;
		constexpr GLuint kColorLocation = 1;
		constexpr GLuint kNormalLocation = 2;
		constexpr GLuint kUvLocation = 3;
		constexpr GLuint kExtraLocation0 = 4;
		constexpr GLuint kExtraLocation1 = 5;

		m_backend.BindVertexArray(array);
		glBindBuffer(GL_ARRAY_BUFFER, buffer);
		const auto stride = static_cast<GLsizei>(p_request.stride);
		glVertexAttribPointer(kPositionLocation, 4, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
		glVertexAttribPointer(kColorLocation, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (GLvoid*)16);
		glVertexAttribPointer(kNormalLocation, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)20);
		glVertexAttribPointer(kUvLocation, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)32);
		glEnableVertexAttribArray(kPositionLocation);
		glEnableVertexAttribArray(kColorLocation);
		glEnableVertexAttribArray(kNormalLocation);
		glEnableVertexAttribArray(kUvLocation);

		// A custom effect's per-vertex data sits at the end of each vertex; attributes the shader
		// does not declare cost nothing, but one it declares and we leave enabled would read a
		// stale stride, so both are turned off when a run does not carry them.
		if (p_request.extraFloats > 0)
		{
			const size_t extraOffset = p_request.stride - p_request.extraFloats * sizeof(float);
			glVertexAttribPointer(kExtraLocation0,
				static_cast<GLint>(std::min(p_request.extraFloats, 4u)), GL_FLOAT, GL_FALSE,
				stride, (GLvoid*)extraOffset);
			glEnableVertexAttribArray(kExtraLocation0);
			if (p_request.extraFloats > 4)
			{
				glVertexAttribPointer(kExtraLocation1,
					static_cast<GLint>(p_request.extraFloats - 4u), GL_FLOAT, GL_FALSE,
					stride, (GLvoid*)(extraOffset + 4 * sizeof(float)));
				glEnableVertexAttribArray(kExtraLocation1);
			}
			else
			{
				glDisableVertexAttribArray(kExtraLocation1);
			}
		}
		else
		{
			glDisableVertexAttribArray(kExtraLocation0);
			glDisableVertexAttribArray(kExtraLocation1);
		}

		glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(p_request.vertices.size()),
			p_request.vertices.data(), GL_STREAM_DRAW);
	}
	void OpenGLDriver::DrawMesh(const ::Rendering::Backend::DrawMeshRequest& p_request)
	{
		if (p_request.mesh == nullptr || p_request.instances == 0)
		{
			return;
		}

		const auto& mesh = *p_request.mesh;
		if (std::getenv("MOON_FRAME_TRACE") != nullptr)
		{
			const GLubyte* ver = glGetString(GL_VERSION);
			std::fprintf(stderr, "[dm] mesh=%p sub=%d vao=%u idx=%u vtx=%u inst=%u prim=%d pass=%s\n",
				static_cast<const void*>(p_request.mesh), p_request.subIndex,
				mesh.GetVertexArrayId(p_request.subIndex),
				mesh.GetIndexCount(p_request.subIndex),
				mesh.GetVertexCount(), p_request.instances, static_cast<int>(p_request.primitiveMode),
				::Rendering::Utils::FrameTrace::GetPass());
			std::fprintf(stderr, "[dm]   context=%s\n", ver != nullptr ? "current" : "MISSING");
			std::fflush(stderr);
		}
		m_backend.BindVertexArray(mesh.GetVertexArrayId(p_request.subIndex));

		// MOON_FRAME_TRACE: a draw that produces nothing says nothing. For the geometry sized
		// draws print the state that decides it - the target actually bound, the program, depth
		// test, and the GL error the call itself raised.
		const bool traceDrawState = std::getenv("MOON_DRAW_TRACE") != nullptr
			&& mesh.GetIndexCount(p_request.subIndex) > 1000;
		if (traceDrawState)
		{
			GLint target = 0;
			GLint program = 0;
			GLint depthFunc = 0;
			GLboolean depthMask = GL_FALSE;
			GLboolean colorMask[4] = { GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE };
			GLint polygonMode[2] = { 0, 0 };
			GLint viewport[4] = { 0, 0, 0, 0 };
			glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
			glGetIntegerv(GL_CURRENT_PROGRAM, &program);
			glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
			glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
			glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
			glGetIntegerv(GL_POLYGON_MODE, polygonMode);
			glGetIntegerv(GL_VIEWPORT, viewport);
			GLint blendSrc = 0, blendDst = 0, blendSrcA = 0, blendDstA = 0;
			glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrc);
			glGetIntegerv(GL_BLEND_DST_RGB, &blendDst);
			glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcA);
			glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstA);
			std::fprintf(stderr,
				"[dm] S pass=%s target=%d prog=%u depth=%d/0x%04x mask=%d cm=%d%d%d%d cull=%d poly=%04x "
				"vp=%d,%d,%dx%d blend=%d/0x%04x/0x%04x/0x%04x/0x%04x err=0x%04x\n",
				::Rendering::Utils::FrameTrace::GetPass(), static_cast<int>(target), program,
				glIsEnabled(GL_DEPTH_TEST), depthFunc, depthMask ? 1 : 0,
				colorMask[0] ? 1 : 0, colorMask[1] ? 1 : 0, colorMask[2] ? 1 : 0,
				colorMask[3] ? 1 : 0, glIsEnabled(GL_CULL_FACE), polygonMode[0],
				viewport[0], viewport[1], viewport[2], viewport[3],
				glIsEnabled(GL_BLEND), static_cast<unsigned>(blendSrc), static_cast<unsigned>(blendDst),
				static_cast<unsigned>(blendSrcA), static_cast<unsigned>(blendDstA), glGetError());
			std::fflush(stderr);

			// The two UBOs the vertex path depends on: a block with no buffer behind it, or a
			// zero matrix, collapses the geometry to a point - which draws nothing and says nothing.
			// The material's own block is dumped too: it is what the shading reads first (albedo,
			// roughness/metallic, emissive), and a stale or unbound one looks like "wrong lighting".
			for (const char* blockName : { "EngineUBO", "ObjectUBO", "MaterialParams" })
			{
				const GLuint block = glGetUniformBlockIndex(program, blockName);
				if (block == GL_INVALID_INDEX)
				{
					std::fprintf(stderr, "[dm] U %s missing\n", blockName);
					continue;
				}
				GLint binding = -1;
				GLint uboName = 0;
				GLint uboStart = 0;
				float row[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				// EngineUBO carries the camera first and the lights after it: six rows is enough to
				// tell "same camera, different light" apart from "same everything" (a light that
				// differs there darkens every lit surface by a constant factor - §8.60).
				float rows[64] = { 0.0f };
				const bool wide = std::strcmp(blockName, "EngineUBO") == 0 || std::strcmp(blockName, "MaterialParams") == 0;
				glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_BINDING, &binding);
				glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, binding, &uboName);
				glGetIntegeri_v(GL_UNIFORM_BUFFER_START, binding, &uboStart);
				if (uboName != 0)
				{
					glGetNamedBufferSubData(uboName, uboStart, sizeof(row), row);
					if (wide)
					{
						glGetNamedBufferSubData(uboName, uboStart, sizeof(rows), rows);
					}
				}
				std::fprintf(stderr, "[dm] U %s blk=%d bind=%d ubo=%d start=%d row0=%.3f,%.3f,%.3f,%.3f\n",
					blockName, static_cast<int>(block), binding, uboName, uboStart,
					row[0], row[1], row[2], row[3]);
				if (wide && uboName != 0)
				{
					// The light rows sit after the camera matrices; a per-block hash says "same
					// contents" without printing a thousand floats, and the first 32 floats still show
					// where a difference starts.
					std::vector<uint8_t> blockBytes(2048, 0);
					glGetNamedBufferSubData(uboName, uboStart,
						static_cast<GLsizeiptr>(blockBytes.size()), blockBytes.data());
					uint64_t hash = 1469598103934665603ull;
					for (const uint8_t byte : blockBytes) { hash = (hash ^ byte) * 1099511628211ull; }
					std::fprintf(stderr, "[dm] U %s hash=%016llx f8=%.3f,%.3f,%.3f,%.3f\n", blockName,
						static_cast<unsigned long long>(hash),
						reinterpret_cast<const float*>(blockBytes.data())[4],
						reinterpret_cast<const float*>(blockBytes.data())[5],
						reinterpret_cast<const float*>(blockBytes.data())[6],
						reinterpret_cast<const float*>(blockBytes.data())[7]);
				}
				if (wide)
				{
					for (int r = 1; r < 8; ++r)
					{
						std::fprintf(stderr, "[dm] U %s row%d=%.3f,%.3f,%.3f,%.3f\n", blockName, r,
							rows[r * 4 + 0], rows[r * 4 + 1], rows[r * 4 + 2], rows[r * 4 + 3]);
					}
				}
			}

			// Every sampler the program declares, with the texture actually bound to its unit.
			GLint uniformCount = 0;
			glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &uniformCount);
			GLchar uniformName[128];
			for (GLint i = 0; i < uniformCount; ++i)
			{
				GLint size = 0;
				GLenum type = 0;
				GLsizei length = 0;
				glGetActiveUniform(program, static_cast<GLuint>(i), sizeof(uniformName), &length,
					&size, &type, uniformName);
				if (type != GL_SAMPLER_2D && type != GL_SAMPLER_2D_ARRAY && type != GL_SAMPLER_CUBE
					&& type != GL_SAMPLER_BUFFER)
				{
					continue;
				}
				const GLint location = glGetUniformLocation(program, uniformName);
				GLint unit = -1;
				GLint texture = 0;
				if (location >= 0)
				{
					glGetUniformiv(program, location, &unit);
					if (unit >= 0)
					{
						// Ask for the binding the sampler's own target keeps: a cube map or a buffer
						// texture is not in GL_TEXTURE_BINDING_2D, so reading that one reported 0 for
						// both and hid the difference it was added to show (docs §8.60).
						switch (type)
						{
						case GL_SAMPLER_CUBE:
							glGetIntegeri_v(GL_TEXTURE_BINDING_CUBE_MAP, unit, &texture);
							break;
						case GL_SAMPLER_BUFFER:
							glGetIntegeri_v(GL_TEXTURE_BINDING_BUFFER, unit, &texture);
							break;
						case GL_SAMPLER_2D_ARRAY:
							glGetIntegeri_v(GL_TEXTURE_BINDING_2D_ARRAY, unit, &texture);
							break;
						default:
							glGetIntegeri_v(GL_TEXTURE_BINDING_2D, unit, &texture);
							break;
						}
					}
				}
				std::fprintf(stderr, "[dm] T %s unit=%d tex=%d\n", uniformName, unit, texture);			}
			std::fflush(stderr);

			// The vertex array itself: an attribute whose buffer or offset is wrong draws
			// collapsed geometry, which looks exactly like "nothing was drawn".
			GLint arrayBinding = 0;
			glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &arrayBinding);
			for (GLuint index = 0; index < 5; ++index)
			{
				GLint enabled = 0, size = 0, type = 0, stride = 0, buffer = 0;
				void* pointer = nullptr;
				glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
				if (enabled == 0)
				{
					break;
				}
				glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &size);
				glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &type);
				glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride);
				glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &buffer);
				glGetVertexAttribPointerv(index, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);
				std::fprintf(stderr, "[dm] A%u array=%d size=%d type=0x%04x stride=%d off=%lld buf=%d\n",
					index, static_cast<int>(arrayBinding), size, static_cast<unsigned>(type), stride,
					static_cast<long long>(reinterpret_cast<intptr_t>(pointer)), buffer);
			}
			GLint elementBuffer = 0;
			glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementBuffer);
			std::fprintf(stderr, "[dm] E elementBuffer=%d\n", elementBuffer);
			std::fflush(stderr);
		}

		const uint32_t indexCount = mesh.GetIndexCount(p_request.subIndex);
		if (indexCount == 0)
		{
			return;
		}

		// The draw call itself. The array and the program are already bound and the state already
		// applied (the material's bind commands and the PipelineState command ran before this one),
		// so nothing is left here but instancing the indices.
		glDrawElementsInstanced(static_cast<GLenum>(p_request.primitiveMode),
			static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, nullptr,
			static_cast<GLsizei>(p_request.instances));
	}

	void OpenGLDriver::PipelineState(::Rendering::Data::PipelineState p_state)
	{
		// Applied in full, every time. The original diffed against m_appliedState; without that cache
		// the state cannot drift out of sync with what GL is really in - which is the failure this
		// had when it was reconstructed (the two modes then rendered the same thing again). The
		// diffing is a throughput optimisation to add back, not a correctness feature.
		using namespace ::Rendering::Settings;

		m_backend.SetCapability(ERenderingCapability::BLEND, p_state.blending);
		if (p_state.blending)
		{
			m_backend.SetBlendingEquation(p_state.blendingEquation);
			m_backend.SetBlendingFunction(p_state.blendingSrcFactor, p_state.blendingDestFactor);
		}

		m_backend.SetCapability(ERenderingCapability::CULL_FACE, p_state.culling);
		if (p_state.culling)
		{
			m_backend.SetCullFace(p_state.cullFace);
		}

		m_backend.SetCapability(ERenderingCapability::DEPTH_TEST, p_state.depthTest);
		if (p_state.depthTest)
		{
			m_backend.SetDepthAlgorithm(p_state.depthFunc);
		}
		m_backend.SetDepthWriting(p_state.depthWriting);
		m_backend.SetColorWriting(p_state.colorWriting.r, p_state.colorWriting.g,
			p_state.colorWriting.b, p_state.colorWriting.a);

		m_backend.SetRasterizationMode(p_state.rasterizationMode);
		m_backend.SetRasterizationLinesWidth(p_state.lineWidth);


		if (p_state.polygonOffsetFill)
		{
			// The offset itself, not only the capability: the depth is reversed (GL_GREATER), so
			// "further away" is a smaller value and the push is negative.
			m_backend.PolygonOffset(-1.0f, -1.0f);
		}
		m_backend.SetCapability(ERenderingCapability::POLYGON_OFFSET_FILL, p_state.polygonOffsetFill);
		m_backend.SetCapability(ERenderingCapability::POLYGON_OFFSET_LINE, p_state.polygonOffsetLine);
		m_backend.SetCapability(ERenderingCapability::SAMPLE_ALPHA_TO_COVERAGE,
			p_state.sampleAlphaToCoverage);
		m_backend.SetCapability(ERenderingCapability::MULTISAMPLE, p_state.multisample);
		m_backend.SetCapability(ERenderingCapability::DITHER, p_state.dither);
		m_backend.SetCapability(ERenderingCapability::SCISSOR_TEST, p_state.scissorTest);

		m_backend.SetCapability(ERenderingCapability::STENCIL_TEST, p_state.stencilTest);
		if (p_state.stencilTest)
		{
			m_backend.SetStencilAlgorithm(p_state.stencilFuncOp,
				static_cast<int32_t>(p_state.stencilFuncRef),
				static_cast<uint32_t>(p_state.stencilFuncMask));
			m_backend.SetStencilMask(p_state.stencilWriteMask);
			m_backend.SetStencilOperations(p_state.stencilOpFail, p_state.depthOpFail, p_state.bothOpFail);
		}

		m_appliedState = p_state;
	}
	void OpenGLDriver::SetClearColor(float p_red, float p_green, float p_blue, float p_alpha)
	{
		m_backend.SetClearColor(p_red, p_green, p_blue, p_alpha);
	}

	void OpenGLDriver::Clear(bool p_colorBuffer, bool p_depthBuffer, bool p_stencilBuffer)
	{
		m_backend.Clear(p_colorBuffer, p_depthBuffer, p_stencilBuffer);
	}
}