#pragma once
#include <string>
#include <array>
#include <memory>
#include "Rendering/Settings/DriverSettings.h"
#include "Rendering/Settings/ERenderingCapability.h"
#include "Rendering/Settings/EPrimitiveMode.h"
#include "Rendering/Settings/ERasterizationMode.h"
#include "Rendering/Settings/EComparaisonAlgorithm.h"
#include "Rendering/Settings/EOperation.h"
#include "Rendering/Settings/ECullFace.h"
#include "Rendering/Settings/ECullingOptions.h"
#include "Rendering/Settings/EPixelDataFormat.h"
#include "Rendering/Settings/EPixelDataType.h"
#include "Rendering/Data/PipelineState.h"
#include "Rendering/Resources/IMesh.h"
#include <Maths/FVector4.h>
#include <Tools/Utils/OptRef.h>
#include "Rendering/Context/IGLContextHost.h"
namespace Rendering::Context
{

	class Driver final
	{
	public:
		Driver(const Settings::DriverSettings& p_driverSettings);

		~Driver();

		/** Tells the driver which context the render module draws on. The window layer creates
		 * it (sharing objects with the window's own context) and hands it over; before that is
		 * set, GL calls go to whatever context the host left current - which is the coupling
		 * this removes. */
		void SetContextHost(IGLContextHost* p_contextHost);
		IGLContextHost* GetContextHost() const;

		/** Re-applies the global GL state the backend sets up (reversed-Z depth range, blend
		 * func, seamless cubemap, debug output). That state is per context, so a freshly made
		 * module context starts without it. Call with the module context current. */
		void ReapplyGlobalState();

		void OnFrameCompleted();
		void SetViewport(
			uint32_t p_x,
			uint32_t p_y,
			uint32_t p_width,
			uint32_t p_height
		);
		void Clear(
			bool p_colorBuffer,
			bool p_depthBuffer,
			bool p_stencilBuffer,
			const Maths::FVector4& p_color = Maths::FVector4::Zero,
			bool p_scissor = false
		);
		/** Sets the scissor rectangle; the test itself is enabled through the
		 * pipeline state (see PipelineState::scissorTest). */
		void SetScissor(
			uint32_t p_x,
			uint32_t p_y,
			uint32_t p_width,
			uint32_t p_height
		);
		void Draw(
			Rendering::Data::PipelineState p_pso,
			const Resources::IMesh& p_mesh,
			Settings::EPrimitiveMode p_primitiveMode = Settings::EPrimitiveMode::TRIANGLES,
			uint32_t p_instances = 1
		);
		void Draw(
			Rendering::Data::PipelineState p_pso,
			const Resources::IMesh& p_mesh,
			int index,
			Settings::EPrimitiveMode p_primitiveMode = Settings::EPrimitiveMode::TRIANGLES,
			uint32_t p_instances = 1
		);
		Data::PipelineState CreatePipelineState() const;
		std::string_view GetVendor() const;
		std::string_view GetHardware() const;
		std::string_view GetVersion() const;
		std::string_view GetShadingLanguageVersion() const;

	public:
		void SetPipelineState(Data::PipelineState p_state);
		void ResetPipelineState();

	private:
		IGLContextHost* m_contextHost = nullptr;
		std::string m_vendor;
		std::string m_hardware;
		std::string m_version;
		std::string m_shadingLanguageVersion;
		Data::PipelineState m_defaultPipelineState;
		Data::PipelineState m_pipelineState;
	};
}
