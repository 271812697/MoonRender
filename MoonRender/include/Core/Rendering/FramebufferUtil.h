#pragma once
#include <cstdint>
#include <Rendering/HAL/Framebuffer.h>

namespace Core::Rendering::FramebufferUtil
{
	void CopyFramebufferColor(
		::Rendering::HAL::Framebuffer& src_framebuffer,
		int src, 
		::Rendering::HAL::Framebuffer& dst_framebuffer, 
		int dst
	);
	void CopyFramebufferDepth(
		::Rendering::HAL::Framebuffer& src_framebuffer,
		::Rendering::HAL::Framebuffer& dst_framebuffer
	);
	void SetupFramebuffer(
		::Rendering::HAL::Framebuffer& p_framebuffer,
		const ::Rendering::Settings::TextureDesc& p_textureDesc,
		bool p_useDepth = true,
		bool p_useStencil = false,
		bool useMulSample = false
	);
	void SetupFramebuffer(
		::Rendering::HAL::Framebuffer& p_framebuffer,
		uint32_t p_width = 0,
		uint32_t p_height = 0,
		bool p_useDepth = true,
		bool p_useStencil = false,
		bool p_useMipMaps = false,
		bool useMulSample = false
	);
	/** Same as SetupFramebuffer, but recorded as a command when a driver is available: the
	 * attachments are GL, and with a render thread they belong to the side that owns the context.
	 * The direct form stays for the paths that have no driver yet (resource loading). */
	void SetupFramebufferWithDriver(
		::Rendering::HAL::Framebuffer& p_framebuffer,
		const ::Rendering::Settings::TextureDesc& p_textureDesc,
		bool p_useDepth = true,
		bool p_useStencil = false,
		bool p_useMultiSample = false
	);
	void SetupFramebufferWithDriver(
		::Rendering::HAL::Framebuffer& p_framebuffer,
		uint32_t p_width,
		uint32_t p_height,
		bool p_useDepth = true,
		bool p_useStencil = false,
		bool p_useMipMaps = false,
		bool p_useMultiSample = false
	);
}