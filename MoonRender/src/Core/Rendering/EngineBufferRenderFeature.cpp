#include <tracy/Tracy.hpp>

#include <Core/Rendering/EngineBufferRenderFeature.h>
#include <Core/Rendering/EngineDrawableDescriptor.h>
#include <Core/Rendering/EngineUBO.h>
#include <Rendering/Core/CompositeRenderer.h>

namespace Core::Rendering
{
	// The block itself lives in EngineUBO.h, shared with the editor's material preview (which
	// uploads its own camera into a buffer of its own). The uploads below address fields by
	// offsetof, so adding a field can no longer leave a hand-counted offset behind.
	constexpr size_t kUBOSize = sizeof(EngineUBO);

	struct LateUboData {
		bool enableClip = false;
	};
}

Core::Rendering::EngineBufferRenderFeature::EngineBufferRenderFeature(
	::Rendering::Core::CompositeRenderer& p_renderer,
	::Rendering::Features::EFeatureExecutionPolicy p_executionPolicy
) : 
	ARenderFeature(p_renderer, p_executionPolicy)
{
	m_engineBuffer = std::make_unique<::Rendering::HAL::UniformBuffer>();
	m_engineBuffer->Allocate(kUBOSize, ::Rendering::Settings::EAccessSpecifier::STREAM_DRAW);
	m_startTime = std::chrono::high_resolution_clock::now();
	m_LateUboData = new LateUboData();
}

Core::Rendering::EngineBufferRenderFeature::~EngineBufferRenderFeature()
{
	if (m_LateUboData) {
		delete m_LateUboData;
	}
}

void Core::Rendering::EngineBufferRenderFeature::SetViewPos(const Maths::FVector3& viewPos)
{

	m_engineBuffer->Upload(&viewPos, ::Rendering::HAL::BufferMemoryRange{
		.offset = sizeof(Maths::FMatrix4)*3, // Skip uploading the first matrix (Model matrix)
		.size = sizeof(Maths::FVector3) });
}

void Core::Rendering::EngineBufferRenderFeature::SetCamera(const ::Rendering::Entities::Camera& p_camera)
{
	const Maths::FMatrix4 view = Maths::FMatrix4::Transpose(p_camera.GetViewMatrix());
	const Maths::FMatrix4 projection = Maths::FMatrix4::Transpose(p_camera.GetProjectionMatrix());

	m_engineBuffer->Upload(&view, ::Rendering::HAL::BufferMemoryRange{
		.offset = offsetof(EngineUBO, ubo_View),
		.size = sizeof(Maths::FMatrix4) });

	m_engineBuffer->Upload(&projection, ::Rendering::HAL::BufferMemoryRange{
		.offset = offsetof(EngineUBO, ubo_Projection),
		.size = sizeof(Maths::FMatrix4) });

	m_engineBuffer->Upload(&p_camera.GetPosition(), ::Rendering::HAL::BufferMemoryRange{
		.offset = offsetof(EngineUBO, ubo_ViewPos),
		.size = sizeof(Maths::FVector3) });

	const int cameraType = p_camera.GetProjectionMode() == ::Rendering::Settings::EProjectionMode::ORTHOGRAPHIC ? 0 : 1;
	m_engineBuffer->Upload(&cameraType, ::Rendering::HAL::BufferMemoryRange{
		.offset = offsetof(EngineUBO, ubo_CameraType),
		.size = sizeof(int) });
}

void Core::Rendering::EngineBufferRenderFeature::SetClipPlane(float x, float y, float z, float w)
{
	
	Maths::FVector4 plane = {x,y,z,w};
	m_engineBuffer->Upload(&plane, ::Rendering::HAL::BufferMemoryRange{
	.offset = offsetof(EngineUBO, ubo_plane),
	.size = sizeof(Maths::FVector4)
		});
}

void Core::Rendering::EngineBufferRenderFeature::SetMirrorPlane(float x, float y, float z, float w)
{
	SetMirrorPlane(Maths::FMatrix4::MirrorPlane(x, y, z, w));
}

void Core::Rendering::EngineBufferRenderFeature::SetMirrorPlane(const Maths::FMatrix4& matrix)
{
	Maths::FMatrix4 mat = Maths::FMatrix4::Transpose(matrix);
	m_engineBuffer->Upload(&mat, ::Rendering::HAL::BufferMemoryRange{
	.offset = offsetof(EngineUBO, ubo_MirroPlaneMatrix),
	.size = sizeof(Maths::FMatrix4)
		});
}

void Core::Rendering::EngineBufferRenderFeature::SetViewMatrix(const Maths::FMatrix4& matrix)
{
	Maths::FMatrix4 mat = Maths::FMatrix4::Transpose(matrix);
	m_engineBuffer->Upload(&mat, ::Rendering::HAL::BufferMemoryRange{
			.offset = offsetof(EngineUBO, ubo_View),
		.size = sizeof(Maths::FMatrix4)
		});
}

void Core::Rendering::EngineBufferRenderFeature::EnableClip(bool flag)
{
	m_LateUboData->enableClip = flag?1:0;
}

bool Core::Rendering::EngineBufferRenderFeature::IsEnableClip()
{
	return m_LateUboData->enableClip;
}

void Core::Rendering::EngineBufferRenderFeature::OnBeginFrame(const ::Rendering::Data::FrameDescriptor& p_frameDescriptor)
{
	assert(p_frameDescriptor.camera.has_value()&&"Camera is not set in the frame descriptor");
	
	auto currentTime = std::chrono::high_resolution_clock::now();
	auto elapsedTime = std::chrono::duration_cast<std::chrono::duration<float>>(currentTime - m_startTime);

	const Maths::FMatrix4 view = Maths::FMatrix4::Transpose(p_frameDescriptor.camera->GetViewMatrix());
	const Maths::FMatrix4 projection = Maths::FMatrix4::Transpose(p_frameDescriptor.camera->GetProjectionMatrix());
	const Maths::FVector3 viewPos = p_frameDescriptor.camera->GetPosition();
	const int cameraType = p_frameDescriptor.camera->GetProjectionMode() == ::Rendering::Settings::EProjectionMode::ORTHOGRAPHIC ? 0 : 1;
	const float time = elapsedTime.count();
	const int screenWidth = static_cast<int>(p_frameDescriptor.renderWidth);
	const int screenHeight = static_cast<int>(p_frameDescriptor.renderHeight);
	const int enableClip = m_LateUboData->enableClip;

	m_engineBuffer->Upload(&view, ::Rendering::HAL::BufferMemoryRange{
		.offset = offsetof(EngineUBO, ubo_View), .size = sizeof(view) });
	m_engineBuffer->Upload(&projection, ::Rendering::HAL::BufferMemoryRange{
		.offset = offsetof(EngineUBO, ubo_Projection), .size = sizeof(projection) });
	m_engineBuffer->Upload(&viewPos, ::Rendering::HAL::BufferMemoryRange{
		.offset = offsetof(EngineUBO, ubo_ViewPos), .size = sizeof(viewPos) });
	m_engineBuffer->Upload(&cameraType, ::Rendering::HAL::BufferMemoryRange{
		.offset = offsetof(EngineUBO, ubo_CameraType), .size = sizeof(cameraType) });
	m_engineBuffer->Upload(&time, ::Rendering::HAL::BufferMemoryRange{
		.offset = offsetof(EngineUBO, ubo_Time), .size = sizeof(time) });
	m_engineBuffer->Upload(&screenWidth, ::Rendering::HAL::BufferMemoryRange{
		.offset = offsetof(EngineUBO, ubo_ScreenWidth), .size = sizeof(screenWidth) });
	m_engineBuffer->Upload(&screenHeight, ::Rendering::HAL::BufferMemoryRange{
		.offset = offsetof(EngineUBO, ubo_ScreenHeight), .size = sizeof(screenHeight) });
	m_engineBuffer->Upload(&enableClip, ::Rendering::HAL::BufferMemoryRange{
		.offset = offsetof(EngineUBO, ubo_enableClip), .size = sizeof(enableClip) });

	m_engineBuffer->Bind(0);
}

void Core::Rendering::EngineBufferRenderFeature::OnEndFrame()
{
	m_engineBuffer->Unbind();
}

void Core::Rendering::EngineBufferRenderFeature::OnBeforeDraw(::Rendering::Data::PipelineState& p_pso, const ::Rendering::Entities::Drawable& p_drawable)
{
	ZoneScoped;

	Tools::Utils::OptRef<const EngineDrawableDescriptor> descriptor;

	if (p_drawable.TryGetDescriptor<EngineDrawableDescriptor>(descriptor))
	{
		const auto modelMatrix = Maths::FMatrix4::Transpose(descriptor->modelMatrix);
		
		// Upload model matrix (First matrix in the UBO)
		m_engineBuffer->Upload(&modelMatrix, ::Rendering::HAL::BufferMemoryRange{
			.offset = offsetof(EngineUBO, ubo_Model),
			.size = sizeof(modelMatrix)
		});

		// Upload user matrix (Last matrix in the UBO)
		m_engineBuffer->Upload(&descriptor->userMatrix, ::Rendering::HAL::BufferMemoryRange{
			.offset = kUBOSize - sizeof(modelMatrix),
			.size = sizeof(modelMatrix)
		});
	}
}
