#include <glad/glad.h>
#include <cstdio>
#include <cstdlib>
#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/FramebufferRequests.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/HAL/Common/HALDriverAccess.h>
#include <Rendering/HAL/OpenGL/GLFramebuffer.h>
#include <Rendering/HAL/OpenGL/GLRenderbuffer.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>

template<>
template<>
void Rendering::HAL::GLFramebuffer::Attach(
	std::shared_ptr<GLRenderbuffer> p_toAttach,
	Settings::EFramebufferAttachment p_attachment,
	uint32_t p_index,
	std::optional<uint32_t> p_layer
)
{

	const auto attachmentIndex = EnumToValue<GLenum>(p_attachment) + static_cast<GLenum>(p_index);

	::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
	if (driver != nullptr)
	{
		// Same as the texture attachment above: the target and the renderbuffer are named here,
		// the attach happens where the context is.
		::Rendering::Backend::AttachRenderbufferRequest request;
		request.framebufferHandleId = m_context.handle.GetId();
		request.renderbufferHandleId = p_toAttach->GetHandleId();
		request.attachment = p_attachment;
		request.index = p_index;
		driver->EnqueueAttachRenderbuffer(request);
		m_context.attachments[attachmentIndex] = p_toAttach;
		return;
	}

	p_toAttach->Bind();
	glNamedFramebufferRenderbuffer(GetID(), attachmentIndex, GL_RENDERBUFFER, p_toAttach->GetID());
	m_context.attachments[attachmentIndex] = p_toAttach;
}

template<>
template<>
void Rendering::HAL::GLFramebuffer::Attach(
	std::shared_ptr<GLTexture> p_toAttach,
	Settings::EFramebufferAttachment p_attachment,
	uint32_t p_index,
	std::optional<uint32_t> p_layer
)
{

	const auto attachmentIndex = EnumToValue<GLenum>(p_attachment) + static_cast<GLenum>(p_index);
	constexpr uint32_t kMipMapLevel = 0;

	::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
	if (p_toAttach->GetHandleId() != 0 && !p_toAttach->HasBackendName() && driver != nullptr)
	{
		// The texture is a front-end object and the target is a handle: attaching is GL, so it is
		// recorded with the two handles and done where the context is (Filament's
		// createRenderTarget naming its attachments instead of making them).
		::Rendering::Backend::AttachTextureRequest request;
		request.framebufferHandleId = m_context.handle.GetId();
		request.textureHandleId = p_toAttach->GetHandleId();
		request.attachment = p_attachment;
		request.index = p_index;
		request.level = kMipMapLevel;
		request.useLayer = p_layer.has_value();
		request.layer = p_layer.value_or(0);
		driver->EnqueueAttachTexture(request);

		// The attachments list is what GetSize/Resize/GetAttachment read on this side; the GL
		// object stays where it was made.
		m_context.attachments[attachmentIndex] = p_toAttach;
		return;
	}

	if (p_layer.has_value())
	{
		glNamedFramebufferTextureLayer(GetID(), attachmentIndex, p_toAttach->GetID(), kMipMapLevel, p_layer.value());
	}
	else
	{

		glNamedFramebufferTexture(GetID(), attachmentIndex, p_toAttach->GetID(), kMipMapLevel);

		if (std::getenv("MOON_FRAME_TRACE") != nullptr)
		{
			const GLenum status = glCheckNamedFramebufferStatus(GetID(), GL_FRAMEBUFFER);
			std::fprintf(stderr, "[fbo] attach fbo=%u att=0x%x tex=%u status=0x%04x\n",
				GetID(), attachmentIndex, p_toAttach->GetID(), status);
			std::fflush(stderr);
		}		
	}

	m_context.attachments[attachmentIndex] = p_toAttach;
}
template<>
template<>
void Rendering::HAL::GLFramebuffer::AttachLevel(
	std::shared_ptr<GLTexture> p_toAttach,
	Settings::EFramebufferAttachment p_attachment,
	uint32_t p_index,
	uint32_t p_level ,
	std::optional<uint32_t> p_layer
)
{

	const auto attachmentIndex = EnumToValue<GLenum>(p_attachment) + static_cast<GLenum>(p_index);

	::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
	if (p_toAttach->GetHandleId() != 0 && !p_toAttach->HasBackendName() && driver != nullptr)
	{
		// The sky capture's prefilter binds one mip level and one cube face at a time: the level
		// and the layer travel with the attach (Filament's TargetBufferInfo).
		::Rendering::Backend::AttachTextureRequest request;
		request.framebufferHandleId = m_context.handle.GetId();
		request.textureHandleId = p_toAttach->GetHandleId();
		request.attachment = p_attachment;
		request.index = p_index;
		request.level = p_level;
		request.useLayer = p_layer.has_value();
		request.layer = p_layer.value_or(0);
		driver->EnqueueAttachTexture(request);
		m_context.attachments[attachmentIndex] = p_toAttach;
		return;
	}

	if (p_layer.has_value())
	{
		glNamedFramebufferTextureLayer(GetID(), attachmentIndex, p_toAttach->GetID(), p_level, p_layer.value());
	}
	else
	{

		glNamedFramebufferTexture(GetID(), attachmentIndex, p_toAttach->GetID(),p_level);
	}

	m_context.attachments[attachmentIndex] = p_toAttach;
}

template<>
Rendering::HAL::GLFramebuffer::TFramebuffer(std::string_view p_debugName) :
	m_context{ .debugName = std::string{p_debugName} }
{
	// A framebuffer is a container object: like Filament's createRenderTargetS/R pair, the handle
	// is taken here and the name is made where the context is. The setup is synchronous for the
	// caller (what names this target next - an attach, a bind - is recorded from this side and runs
	// after it in the same batch), and it is the same with or without a render thread
	// (docs/RenderThreadPlan.md §8.72).
	m_context.handle = ::Rendering::Backend::GetHandleAllocator().Allocate<::Rendering::Backend::HwFramebuffer>();
	::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
	if (driver != nullptr)
	{
		driver->EnqueueCreateFramebuffer(m_context.handle.GetId());
	}
	else
	{
		::Rendering::Backend::Records::CreateFramebuffer(m_context.handle);
	}

	// MOON_FRAME_TRACE: a target with no debug name is otherwise invisible in the [fbo] bind
	// trace - only an id that cannot be traced back to the object a pass binds. Printed after the
	// create command has run, so the GL name is the one the executor will use.
	if (std::getenv("MOON_FRAME_TRACE") != nullptr)
	{
		std::fprintf(stderr, "[fbo] new name=%s handle=%u fbo=%u\n",
			m_context.debugName.c_str(), m_context.handle.GetId(), GetID());
		std::fflush(stderr);
	}
}

template<>
Rendering::HAL::GLFramebuffer::~TFramebuffer()
{
	if (m_context.handle)
	{
		::Rendering::Backend::Records::DestroyFramebuffer(m_context.handle);
		m_context.handle.clear();
	}
}
template<>
void Rendering::HAL::GLFramebuffer::Bind() const
{
	glBindFramebuffer(GL_FRAMEBUFFER, GetID());
}

template<>
void Rendering::HAL::GLFramebuffer::Unbind() const
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool Rendering::HAL::GLFramebuffer::Validate()
{
	// With a driver the target's attachments are attached on the executor, so the status here would
	// describe a state this thread cannot see. The bind trace ([fbo] bind ... status=) is where it
	// is checked instead - in both configurations, because the recording code is the same
	// (docs/RenderThreadPlan.md §8.72).
	::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
	if (driver != nullptr)
	{
		return true;
	}

	const GLenum status = glCheckNamedFramebufferStatus(GetID(), GL_FRAMEBUFFER);

	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		return m_context.valid = false;
	}

	return m_context.valid = true;
}

template<>
bool Rendering::HAL::GLFramebuffer::IsValid() const
{
	return m_context.valid;
}

template<>
template<>
Tools::Utils::OptRef<Rendering::HAL::GLTexture> Rendering::HAL::GLFramebuffer::GetAttachment(
	Rendering::Settings::EFramebufferAttachment p_attachment,
	uint32_t p_index
) 
{
	const auto attachmentIndex = EnumToValue<GLenum>(p_attachment) + static_cast<GLenum>(p_index);

	if (m_context.attachments.contains(attachmentIndex))
	{
		auto attachment = m_context.attachments.at(attachmentIndex);

		if (auto pval = std::get_if<std::shared_ptr<GLTexture>>(&attachment); pval && *pval)
		{
			return **pval;
		}
	}

	return std::nullopt;
}

template<>
template<>
Tools::Utils::OptRef<Rendering::HAL::GLRenderbuffer> Rendering::HAL::GLFramebuffer::GetAttachment(
	Rendering::Settings::EFramebufferAttachment p_attachment,
	uint32_t p_index
) 
{
	const auto attachmentIndex = EnumToValue<GLenum>(p_attachment) + static_cast<GLenum>(p_index);

	if (m_context.attachments.contains(attachmentIndex))
	{
		auto attachment = m_context.attachments.at(attachmentIndex);

		if (auto pval = std::get_if<std::shared_ptr<GLRenderbuffer>>(&attachment); pval && *pval)
		{
			return **pval;
		}
	}

	return std::nullopt;
}

template<>
template<>
std::shared_ptr<Rendering::HAL::GLTexture> Rendering::HAL::GLFramebuffer::GetAttachmentShared(
	Rendering::Settings::EFramebufferAttachment p_attachment,
	uint32_t p_index
) const
{
	const auto attachmentIndex = EnumToValue<GLenum>(p_attachment) + static_cast<GLenum>(p_index);

	if (auto it = m_context.attachments.find(attachmentIndex); it != m_context.attachments.end())
	{
		if (const auto pval = std::get_if<std::shared_ptr<GLTexture>>(&it->second); pval != nullptr)
		{
			return *pval;
		}
	}

	return nullptr;
}

template<>
void Rendering::HAL::GLFramebuffer::Resize(uint16_t p_width, uint16_t p_height)
{
	

	for (auto& attachment : m_context.attachments)
	{
		if (const auto pval = std::get_if<std::shared_ptr<GLTexture>>(&attachment.second); pval && *pval)
		{
			(*pval)->Resize(p_width, p_height);
		}
		else if (const auto* pval = std::get_if<std::shared_ptr<GLRenderbuffer>>(&attachment.second); pval && *pval)
		{
			(*pval)->Resize(p_width, p_height);
		}
	}
}

template<>
void Rendering::HAL::GLFramebuffer::SetDrawBuffers(const std::vector<uint32_t>& indices)
{
	::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
	if (driver != nullptr)
	{
		::Rendering::Backend::SetFramebufferDrawBuffersRequest request;
		request.framebufferHandleId = m_context.handle.GetId();
		request.indices = indices;
		driver->EnqueueSetFramebufferDrawBuffers(request);
		return;
	}

	size_t n_index = indices.size();
	GLenum* buffers = new GLenum[n_index];

	for (GLenum i = 0; i < n_index; i++) {
		// the `layout(location = i) out` variable will write to this attachment
		GLuint index = indices[i];
		
		*(buffers + i) = GL_COLOR_ATTACHMENT0 + index;
	}

	glNamedFramebufferDrawBuffers(GetID(), n_index, buffers);
	delete[] buffers;
}

template<>
void Rendering::HAL::GLFramebuffer::SetTargetDrawBuffer(std::optional<uint32_t> p_index)
{
	::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
	if (driver != nullptr)
	{
		// One draw buffer is the single-entry form of SetDrawBuffers; the sky capture switches the
		// cube face it writes this way.
		::Rendering::Backend::SetFramebufferDrawBuffersRequest request;
		request.framebufferHandleId = m_context.handle.GetId();
		if (p_index.has_value())
		{
			request.indices.push_back(p_index.value());
		}
		driver->EnqueueSetFramebufferDrawBuffers(request);
		return;
	}

	if (p_index.has_value())
	{
		glNamedFramebufferDrawBuffer(GetID(), GL_COLOR_ATTACHMENT0 + p_index.value());
	}
	else
	{
		glNamedFramebufferDrawBuffer(GetID(), GL_NONE);
	}
}

template<>
void Rendering::HAL::GLFramebuffer::SetTargetReadBuffer(std::optional<uint32_t> p_index)
{
	

	if (p_index.has_value())
	{
		glNamedFramebufferReadBuffer(GetID(), GL_COLOR_ATTACHMENT0 + p_index.value());
	}
	else
	{
		glNamedFramebufferReadBuffer(GetID(), GL_NONE);
	}
}

template<>
uint32_t Rendering::HAL::GLFramebuffer::GetID() const
{
	return ::Rendering::Backend::Records::FramebufferName(m_context.handle);
}

template<>
std::pair<uint16_t, uint16_t> Rendering::HAL::GLFramebuffer::GetSize(
	Settings::EFramebufferAttachment p_attachment
) const
{
	for (auto& attachment : m_context.attachments)
	{
		if (const auto pval = std::get_if<std::shared_ptr<GLTexture>>(&attachment.second); pval && *pval)
		{
			return {
				(*pval)->GetDesc().width,
				(*pval)->GetDesc().height
			};
		}
		else if (const auto* pval = std::get_if<std::shared_ptr<GLRenderbuffer>>(&attachment.second); pval && *pval)
		{
			return {
				(*pval)->GetWidth(),
				(*pval)->GetHeight()
			};
		}
	}
	return { {}, {} }; // <-- not an emoji
}

template<>
void Rendering::HAL::GLFramebuffer::BlitToBackBuffer(uint16_t p_backBufferWidth, uint16_t p_backBufferHeight) const
{
	
	auto [width, height] = GetSize(Settings::EFramebufferAttachment::COLOR);

	glBlitNamedFramebuffer(
		GetID(), 0,
		0, 0, width, height,
		0, 0, p_backBufferWidth, p_backBufferHeight,
		GL_COLOR_BUFFER_BIT, GL_LINEAR
	);
}

template<>
void Rendering::HAL::GLFramebuffer::ReadPixels(
	uint32_t p_x,
	uint32_t p_y,
	uint32_t p_width,
	uint32_t p_height,
	Settings::EPixelDataFormat p_format,
	Settings::EPixelDataType p_type,
	void* p_data) const
{

	Bind();
	glReadPixels(
		p_x, p_y,
		p_width,
		p_height,
		EnumToValue<GLenum>(p_format),
		EnumToValue<GLenum>(p_type),
		p_data
	);
	Unbind();
}
template<>
void Rendering::HAL::GLFramebuffer::ReadPixelsToBuffer(
	const Rendering::HAL::GLBuffer& p_buffer,
	uint64_t p_offset,
	uint32_t p_x,
	uint32_t p_y,
	uint32_t p_width,
	uint32_t p_height,
	Settings::EPixelDataFormat p_format,
	Settings::EPixelDataType p_type) const
{
	Bind();
	glBindBuffer(GL_PIXEL_PACK_BUFFER, p_buffer.GetID());
	glReadPixels(
		p_x, p_y,
		p_width,
		p_height,
		EnumToValue<GLenum>(p_format),
		EnumToValue<GLenum>(p_type),
		reinterpret_cast<void*>(static_cast<uintptr_t>(p_offset))
	);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	Unbind();
}

template<>
void Rendering::HAL::GLFramebuffer::Clear(Settings::EFramebufferAttachment p_attachment,int index)
{
	const GLfloat clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	const GLfloat clear_depth = 1.0f;
	const GLint clear_stencil = 0;
	// clear one of the color attachments
	if (p_attachment== Settings::EFramebufferAttachment::COLOR) {
		glClearNamedFramebufferfv(GetID(), GL_COLOR, index, clear_color);
	}
	// clear the depth buffer
	else if (p_attachment == Settings::EFramebufferAttachment::DEPTH) {
		glClearNamedFramebufferfv(GetID(), GL_DEPTH, 0, &clear_depth);
	}
	// clear the stencil buffer
	else if (p_attachment == Settings::EFramebufferAttachment::STENCIL) {
		glClearNamedFramebufferiv(GetID(), GL_STENCIL, 0, &clear_stencil);
	}
}
template<>
const std::string& Rendering::HAL::GLFramebuffer::GetDebugName() const
{
	return m_context.debugName;
}
