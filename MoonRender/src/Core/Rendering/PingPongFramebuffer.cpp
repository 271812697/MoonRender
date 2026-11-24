#include <format>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <Core/Rendering/PingPongFramebuffer.h>

Core::Rendering::PingPongFramebuffer::PingPongFramebuffer(std::string_view p_debugName) :
	CircularIterator<::Rendering::Data::Framebuffer, 2>(m_framebuffers),
	m_framebuffers{
		::Rendering::Data::Framebuffer{std::format("{}PingPong{}", p_debugName, 0)},
		::Rendering::Data::Framebuffer{std::format("{}PingPong{}", p_debugName, 1)}
	}
{

	if (std::getenv("MOON_FRAME_TRACE") != nullptr)
	{
		std::fprintf(stderr, "[pp] constructed %s ids=%u,%u\n", std::string(p_debugName).c_str(),
			m_framebuffers[0].GetID(), m_framebuffers[1].GetID());
		std::fflush(stderr);
	}
}

std::array<::Rendering::Data::Framebuffer, 2>& Core::Rendering::PingPongFramebuffer::GetFramebuffers()
{
	return m_framebuffers;
}
