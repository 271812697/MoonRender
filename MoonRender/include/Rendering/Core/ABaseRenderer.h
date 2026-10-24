#pragma once
#include <atomic>
#include "Rendering/Core/IRenderer.h"
#include "Rendering/Data/FrameInfo.h"
#include "Rendering/Resources/IMesh.h"
#include "Rendering/Resources/Texture.h"
#include "Rendering/Entities/Drawable.h"
#include "Rendering/Settings/EBlitFlags.h"
#include "Rendering/Context/Driver.h"
namespace Rendering::Core
{
	class ABaseRenderer : public IRenderer
	{
	public:
		/**
		* Constructor of the base renderer
		* @param p_driver
		*/
		ABaseRenderer(Context::Driver& p_driver);

		/**
		* Destructor of the base renderer
		*/
		virtual ~ABaseRenderer() = default;

		/**
		* Begin Frame
		* @param p_frameDescriptor
		*/
		virtual void BeginFrame(const Data::FrameDescriptor& p_frameDescriptor);

		/**
		* End Frame
		*/
		virtual void EndFrame();

		/**
		* Returns a reference to the current frame descriptor
		* @note Cannot be called outside of a frame drawing operation
		*/
		const Data::FrameDescriptor& GetFrameDescriptor() const;

		/**
		* Create a pipeline state object.
		* The settings are set with default settings provided by the renderer
		*/
		Data::PipelineState CreatePipelineState() const;

		/**
		* Returns true if the renderer is currently drawing a frame
		*/
		bool IsDrawing() const;

		/**
		* Set the viewport
		* @param p_x
		* @param p_y
		* @param p_width
		* @param p_height
		*/
		void SetViewport(uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_height);

		/** Sets the scissor rectangle used when the pipeline state enables the
		 * scissor test (see Clear(..., p_scissor) and PipelineState). */
		void SetScissor(uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_height);

		/**
		* Clear the screen
		* @param p_colorBuffer
		* @param p_depthBuffer
		* @param p_stencilBuffer
		* @param p_color
		*/
		virtual void Clear(
			bool p_colorBuffer,
			bool p_depthBuffer,
			bool p_stencilBuffer,
			const Maths::FVector4& p_color = Maths::FVector4::Zero,
			bool p_scissor = false
		);

		/**
		* Draw a fullscreen quad with the given material
		* @param p_pso
		* @param p_src
		* @param p_dst
		* @param p_material
		* @param p_flags
		*/
		virtual void Blit(
			Rendering::Data::PipelineState p_pso,
			Rendering::HAL::Framebuffer& p_src,
			Rendering::HAL::Framebuffer& p_dst,
			Rendering::Data::Material& p_material,
			Rendering::Settings::EBlitFlags p_flags = Rendering::Settings::EBlitFlags::DEFAULT
		);
		void Present(Rendering::HAL::Framebuffer& p_src);
		void Present(Rendering::HAL::Framebuffer& p_src, Rendering::Data::Material& mat);
		void Present(Rendering::HAL::Texture& p_src, Rendering::Data::Material& mat);
		void Present(Rendering::Data::Material& mat);
		void Present(Rendering::HAL::Texture& p_src);
		/**
		* Returns true if the entity is drawable. Should be used before submitting an entity.
		* @param p_drawable
		*/
		virtual bool IsDrawable(const Entities::Drawable& p_drawable) const;

		/**
		* Draw a drawable entity
		* @note Any submitted entity should be drawable (Use IsDrawable before)
		* @param p_pso
		* @param p_drawable
		*/
		virtual void DrawEntity(
			Rendering::Data::PipelineState p_pso,
			const Entities::Drawable& p_drawable
		);
	public:
		Rendering::Resources::Mesh m_unitQuad;
		Rendering::Resources::Mesh m_unitCube;
		/** The view's split-sum DFG LUT: global, bound for every material that declares
		 * _BRDFLut (see Material::UploadParameterBlock). Set by CompositeRenderer each frame. */
		void SetBrdfLut(Rendering::HAL::Texture* p_lut) { m_brdfLut = p_lut; }
		/** The view's environment (filament's IndirectLight): the irradiance cube (diffuse) and
		 * the prefiltered cube (specular). Bound per draw like the DFG LUT, so a material
		 * instance created before the sky pass baked them still samples the view's environment
		 * instead of the empty black cube. */
		void SetEnvironmentTextures(Rendering::HAL::Texture* p_irradianceCube, Rendering::HAL::Texture* p_prefilterCube)
		{
			m_irradianceCube = p_irradianceCube;
			m_prefilterCube = p_prefilterCube;
		}
	protected:
		Data::FrameDescriptor m_frameDescriptor;
		Context::Driver& m_driver;
		Rendering::HAL::Texture m_emptyTexture2D;
		Rendering::HAL::Texture m_emptyTextureCube;
		/** The neutral tangent-space normal (0.5, 0.5, 1.0), bound for a sampler declared
		 * `fallback : flatNormal` when the material instance has no map in that slot. */
		Rendering::HAL::Texture m_flatNormalTexture;
		Rendering::HAL::Texture* m_brdfLut = nullptr;
		Rendering::HAL::Texture* m_irradianceCube = nullptr;
		Rendering::HAL::Texture* m_prefilterCube = nullptr;
		
		Rendering::Data::PipelineState m_basePipelineState;
		bool m_isDrawing;
		std::optional<Rendering::Data::MaterialSignatureSet> m_previousMaterialSignature = std::nullopt;
		Rendering::Resources::Shader* m_presentShader;
		Rendering::Data::Material m_presentMaterial;
	private:
		static std::atomic_bool s_isDrawing;
	};
}
