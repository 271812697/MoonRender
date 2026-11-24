#pragma once
#include <array>
#include <Rendering/Data/Framebuffer.h>
#include <Tools/Utils/CircularIterator.h>

namespace Core::Rendering
{
	/**
	* Convenient ping-pong buffer holding two framebuffers
	*/
	class PingPongFramebuffer : public Tools::Utils::CircularIterator<::Rendering::Data::Framebuffer, 2>
	{
	public:
		/**
		* Create a ping pong buffer
		* @param p_debugName
		*/
		PingPongFramebuffer(std::string_view p_debugName = std::string_view{});

		/**
		* Return the two framebuffers
		*/
		std::array<::Rendering::Data::Framebuffer, 2>& GetFramebuffers();

	private:
		std::array<::Rendering::Data::Framebuffer, 2> m_framebuffers;
	};
}
