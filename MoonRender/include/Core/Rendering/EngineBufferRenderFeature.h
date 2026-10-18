#pragma once
#include <chrono>
#include <map>
#include <memory>
#include <stack>
#include <Rendering/Features/ARenderFeature.h>
#include <Rendering/HAL/UniformBuffer.h>
#include <Rendering/Entities/Camera.h>

namespace Core::Rendering
{
	struct LateUboData;
	class EngineBufferRenderFeature : public ::Rendering::Features::ARenderFeature
	{
	public:

		EngineBufferRenderFeature(
			::Rendering::Core::CompositeRenderer& p_renderer,
			::Rendering::Features::EFeatureExecutionPolicy p_executionPolicy
		);
		~EngineBufferRenderFeature(
		)override;
		void SetViewPos(const Maths::FVector3& viewPos);
		void SetCamera(const ::Rendering::Entities::Camera& p_camera);
		void SetClipPlane(float x,float y,float z,float w);
		void SetMirrorPlane(float x, float y, float z, float w);
		void SetMirrorPlane(const Maths::FMatrix4& matrix);
		void SetViewMatrix(const Maths::FMatrix4& matrix);
		void EnableClip(bool flag);
		bool IsEnableClip();

	protected:
		virtual void OnBeginFrame(const ::Rendering::Data::FrameDescriptor& p_frameDescriptor) override;
		virtual void OnEndFrame() override;
		virtual void OnBeforeDraw(::Rendering::Data::PipelineState& p_pso, const ::Rendering::Entities::Drawable& p_drawable) override;
	protected:
		std::chrono::high_resolution_clock::time_point m_startTime;
		std::unique_ptr<::Rendering::HAL::UniformBuffer> m_engineBuffer;
		/** The per-renderable block: uploaded before every drawable, bound to
		 * kObjectUBOBinding. */
		std::unique_ptr<::Rendering::HAL::UniformBuffer> m_objectBuffer;

		LateUboData* m_LateUboData;
	};
}
