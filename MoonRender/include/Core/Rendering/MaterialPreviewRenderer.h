#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <Rendering/HAL/Framebuffer.h>
#include <Rendering/HAL/Texture.h>
#include <Rendering/HAL/UniformBuffer.h>
#include <Rendering/Resources/Mesh.h>

namespace Rendering::Context { class Driver; }
namespace Rendering::Data { class Material; }

namespace Core::Rendering
{
	/** Renders one material on a preview mesh, offscreen, for the material editor.
	 *
	 * Why it exists: the editor lets a shader be edited and hot reloaded, so it needs to show
	 * what the new material looks like without rebuilding the application. The scene renderer
	 * cannot do that - its DrawEntity goes through the pass machinery and needs a frame
	 * descriptor, a camera and the scene's uniform buffer. This class is the smallest thing
	 * that draws one material: its own framebuffer, its own EngineUBO, a sphere, one draw call.
	 *
	 * It owns GL objects, so the caller has to make the viewport's context current before
	 * calling Render (the editor's ViewerWidget does that).
	 *
	 * Deliberately does not know about Qt: Render hands back RGBA bytes, the panel wraps them
	 * into a QImage. */
	class MaterialPreviewRenderer
	{
	public:
		explicit MaterialPreviewRenderer(::Rendering::Context::Driver& p_driver);
		~MaterialPreviewRenderer();

		MaterialPreviewRenderer(const MaterialPreviewRenderer&) = delete;
		MaterialPreviewRenderer& operator=(const MaterialPreviewRenderer&) = delete;

		/** Draws p_material on the preview sphere into an offscreen target of p_width x
		 * p_height and reads it back into p_pixels (RGBA8, GL row order - bottom row first).
		 * Returns false and fills p_error when the material has no usable shader or the driver
		 * refused it; the message is the driver's own, so its line numbers point into the
		 * .ovfx thanks to the #line directives.
		 *
		 * The caller's viewport is left at p_width x p_height: a preview render happens outside
		 * the frame, so the viewport is restored by whoever draws the next frame. */
		bool Render(::Rendering::Data::Material& p_material, uint32_t p_width, uint32_t p_height,
			std::vector<uint8_t>& p_pixels, std::string& p_error);

	private:
		void EnsureTarget(uint32_t p_width, uint32_t p_height);

		::Rendering::Context::Driver& m_driver;
		::Rendering::HAL::Framebuffer m_framebuffer;
		::Rendering::HAL::UniformBuffer m_engineBuffer;
		::Rendering::HAL::Texture m_white;
		::Rendering::HAL::Texture m_blackCube;
		/** Mesh has no default constructor and is not copyable, so it is held by pointer and
		 * built once in the constructor. */
		std::unique_ptr<::Rendering::Resources::Mesh> m_sphere;
		uint32_t m_width = 0;
		uint32_t m_height = 0;
	};
}
