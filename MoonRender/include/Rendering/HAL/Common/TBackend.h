#pragma once
#include <Rendering/Settings/DriverSettings.h>
#include <Rendering/Settings/ERenderingCapability.h>
#include <Rendering/Settings/EPrimitiveMode.h>
#include <Rendering/Settings/ERasterizationMode.h>
#include <Rendering/Settings/EComparaisonAlgorithm.h>
#include <Rendering/Settings/EOperation.h>
#include <Rendering/Settings/ECullFace.h>
#include <Rendering/Settings/ECullingOptions.h>
#include <Rendering/Settings/EPixelDataFormat.h>
#include <Rendering/Settings/EPixelDataType.h>
#include <Rendering/Settings/EGraphicsBackend.h>
#include <Rendering/Data/PipelineState.h>

namespace Rendering::HAL
{
	/**
	* Backend class that wraps the selected graphics API's context.
	*/
	template<Settings::EGraphicsBackend Backend, class Context>
	class TBackend final
	{
	public:
		std::optional<Data::PipelineState> Init(bool p_debug);
		void OnFrameStarted();
		void OnFrameCompleted();
		void Clear(bool p_colorBuffer, bool p_depthBuffer, bool p_stencilBuffer);
		void ReadPixels(
			uint32_t p_x,
			uint32_t p_y,
			uint32_t p_width,
			uint32_t p_height,
			Settings::EPixelDataFormat p_format,
			Settings::EPixelDataType p_type,
			void* p_data
		);
		void DrawElements(Settings::EPrimitiveMode p_primitiveMode, uint32_t p_indexCount);
		void DrawElementsInstanced(Settings::EPrimitiveMode p_primitiveMode, uint32_t p_indexCount, uint32_t p_instances);
		void DrawArrays(Settings::EPrimitiveMode p_primitiveMode, uint32_t p_vertexCount);
		void DrawArraysInstanced(Settings::EPrimitiveMode p_primitiveMode, uint32_t p_vertexCount, uint32_t p_instances);
		/** Binds a vertex array object by name; 0 unbinds. The draw path records this, so the
		 * backend is the one that knows how an array is bound to the pipeline. */
		void BindVertexArray(uint32_t p_vertexArray);
		void SetClearColor(float p_red, float p_green, float p_blue, float p_alpha);
		void PolygonOffset(float a,float b);
		void SetRasterizationLinesWidth(float p_width);
		void SetRasterizationMode(Settings::ERasterizationMode p_rasterizationMode);
		void SetCapability(Settings::ERenderingCapability p_capability, bool p_value);
		bool GetCapability(Settings::ERenderingCapability p_capability);
		void SetStencilAlgorithm(Rendering::Settings::EComparaisonAlgorithm p_algorithm, int32_t p_reference, uint32_t p_mask);
		void SetDepthAlgorithm(Rendering::Settings::EComparaisonAlgorithm p_algorithm);
		void SetStencilMask(uint32_t p_mask);
		void SetStencilOperations(
			Settings::EOperation p_stencilFail,
			Settings::EOperation p_depthFail,
			Settings::EOperation p_bothPass
		);
		void SetBlendingFunction(
			Settings::EBlendingFactor p_sourceFactor,
			Settings::EBlendingFactor p_destinationFactor
		);
		void SetBlendingEquation(
			Settings::EBlendingEquation p_equation
		);
		void SetCullFace(Settings::ECullFace p_cullFace);
		void SetDepthWriting(bool p_enable);
		void SetColorWriting(bool p_enableRed, bool p_enableGreen, bool p_enableBlue, bool p_enableAlpha);
		/** Binds a framebuffer by name; 0 binds the default one. The render target a pass draws
		 * into is a command now, so the backend is what turns "this target" into a GL bind. */
		void BindFramebuffer(uint32_t p_framebuffer);
		void SetViewport(uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_height);
		/** Sets the scissor rectangle (in framebuffer pixels, origin bottom-left).
		 * The scissor test itself is toggled through the pipeline state. */
		void SetScissor(uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_height);
		std::string GetVendor();
		std::string GetHardware();
		std::string GetVersion();
		std::string GetShadingLanguageVersion();

	private:
		Context m_context;
	};

}
