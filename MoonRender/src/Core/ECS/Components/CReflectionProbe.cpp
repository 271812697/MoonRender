#include <format>
#include <tinyxml2.h>
#include <assert.h>
#include <Core/ECS/Actor.h>
#include <Core/ECS/Components/CReflectionProbe.h>

#include <Rendering/Data/Renderbuffer.h>


namespace
{
	constexpr size_t kUBOSize =
		sizeof(Maths::FVector4) +	// Position (vec3)
		sizeof(Maths::FMatrix4) +	// Rotation (mat3)
		sizeof(Maths::FVector4) +	// Box Center (vec3)
		sizeof(Maths::FVector4) +	// Box Extents (vec3)
		sizeof(float) +				// Brightness (float)
		sizeof(int) +				// Box Projection (bool)
		sizeof(int);                //Local

	constexpr uint32_t kBackBufferIndex = 0; // Cubemap that is being rendered
	constexpr uint32_t kCompleteBufferIndex = 1; // Cubemap that is used

	bool RequiresDoubleBuffering(Core::ECS::Components::CReflectionProbe::ECaptureSpeed p_speed)
	{
		return p_speed != Core::ECS::Components::CReflectionProbe::ECaptureSpeed::SIX_FACES;
	}
}

Core::ECS::Components::CReflectionProbe::CReflectionProbe(ECS::Actor& p_owner) :
	AComponent(p_owner),
	m_framebuffers{ "ReflectionProbeFramebuffer" },
	m_cubemapIterator{ m_cubemaps }
{
	m_uniformBuffer = std::make_unique<::Rendering::Data::BufferObject>(
		::Rendering::Settings::EBufferType::UNIFORM);
	m_uniformBuffer->Allocate(kUBOSize, ::Rendering::Settings::EAccessSpecifier::STREAM_DRAW);

	_AllocateResources();
}

std::string Core::ECS::Components::CReflectionProbe::GetName()
{
	return "Reflection Probe";
}

void Core::ECS::Components::CReflectionProbe::SetRefreshMode(ERefreshMode p_mode)
{
	if (p_mode != m_refreshMode)
	{
		m_refreshMode = p_mode;
	}
}

Core::ECS::Components::CReflectionProbe::ERefreshMode Core::ECS::Components::CReflectionProbe::GetRefreshMode() const
{
	return m_refreshMode;
}

void Core::ECS::Components::CReflectionProbe::SetCaptureSpeed(ECaptureSpeed p_speed)
{
	const bool requiredDoubleBuffering = _IsDoubleBuffered();
	const bool willRequireDoubleBuffering = RequiresDoubleBuffering(p_speed);

	m_captureSpeed = p_speed;
	m_captureFaceIndex = 0; // Reset the capture face index, to make sure we start from the first face again.

	// Progressive uses double buffering, while immediate (6 face per frame) doesn't.
	// This makes sure that the proper resources are allocated for
	// the given refresh mode.
	if (requiredDoubleBuffering != willRequireDoubleBuffering)
	{
		_AllocateResources();
	}
}

Core::ECS::Components::CReflectionProbe::ECaptureSpeed Core::ECS::Components::CReflectionProbe::GetCaptureSpeed() const
{
	return m_captureSpeed;
}

void Core::ECS::Components::CReflectionProbe::SetCapturePosition(const Maths::FVector3& p_position)
{
	m_capturePosition = p_position;
}

const Maths::FVector3& Core::ECS::Components::CReflectionProbe::GetCapturePosition() const
{
	return m_capturePosition;
}

void Core::ECS::Components::CReflectionProbe::SetBrightness(float p_brightness)
{
	m_brightness = p_brightness;
}

float Core::ECS::Components::CReflectionProbe::GetBrightness() const
{
	return m_brightness;
}

void Core::ECS::Components::CReflectionProbe::SetCubemapResolution(uint32_t p_resolution)
{
	assert(p_resolution > 0&& "Cubemap resolution must be greater than 0");
	assert((p_resolution & (p_resolution - 1)) == 0 > 0&&"Cubemap resolution must be a power of 2");

	if (p_resolution != m_resolution)
	{
		m_resolution = p_resolution;
		_AllocateResources();
	}
}

uint32_t Core::ECS::Components::CReflectionProbe::GetCubemapResolution() const
{
	return m_resolution;
}

void Core::ECS::Components::CReflectionProbe::SetInfluencePolicy(EInfluencePolicy p_policy)
{
	m_influencePolicy = p_policy;
}

Core::ECS::Components::CReflectionProbe::EInfluencePolicy Core::ECS::Components::CReflectionProbe::GetInfluencePolicy() const
{
	return m_influencePolicy;
}

void Core::ECS::Components::CReflectionProbe::SetInfluenceSize(const Maths::FVector3& p_size)
{
	m_influenceSize = p_size;
}

const Maths::FVector3& Core::ECS::Components::CReflectionProbe::GetInfluenceSize() const
{
	return m_influenceSize;
}

void Core::ECS::Components::CReflectionProbe::SetBoxProjection(bool p_enabled)
{
	m_boxProjection = p_enabled;
}

bool Core::ECS::Components::CReflectionProbe::IsBoxProjectionEnabled() const
{
	return m_boxProjection;
}

void Core::ECS::Components::CReflectionProbe::RequestCapture(bool p_forceImmediate)
{
	m_captureFaceIndex = 0;

	if (m_captureRequest.has_value() && m_captureRequest->forceImmediate)
	{
		// If a request already exists and is set to force immediate,
		// we ignore the new request.
		return;
	}

	m_captureRequest = {
		.forceImmediate = p_forceImmediate
	};
}

std::shared_ptr<Rendering::HAL::Texture> Core::ECS::Components::CReflectionProbe::GetCubemap() const
{
	const auto& target =
		_IsDoubleBuffered() ?
		m_cubemapIterator[kCompleteBufferIndex] :
		m_cubemaps[0];

	assert(target != nullptr&&"Cubemap is not initialized");
	return target;
}

void Core::ECS::Components::CReflectionProbe::OnSerialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node)
{
	using namespace Core::Helpers;
	Serializer::SerializeUint32(p_doc, p_node, "refresh_mode", static_cast<uint32_t>(m_refreshMode));
	Serializer::SerializeUint32(p_doc, p_node, "capture_speed", static_cast<uint32_t>(m_captureSpeed));
	Serializer::SerializeVec3(p_doc, p_node, "capture_position", m_capturePosition);
	Serializer::SerializeFloat(p_doc, p_node, "brightness", m_brightness);
	Serializer::SerializeUint32(p_doc, p_node, "resolution", m_resolution);
	Serializer::SerializeUint32(p_doc, p_node, "influence_policy", static_cast<uint32_t>(m_influencePolicy));
	Serializer::SerializeVec3(p_doc, p_node, "influence_size", m_influenceSize);
	Serializer::SerializeBoolean(p_doc, p_node, "box_projection", m_boxProjection);
}

void Core::ECS::Components::CReflectionProbe::OnDeserialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node)
{
	using namespace Core::Helpers;

	// Not ideal, but avoids garbage value from erriding the current resolution.
	if (p_node->FirstChildElement("resolution"))
	{
		m_resolution = Serializer::DeserializeInt(p_doc, p_node, "resolution");
	}

	Serializer::DeserializeUint32(p_doc, p_node, "refresh_mode", reinterpret_cast<uint32_t&>(m_refreshMode));
	Serializer::DeserializeUint32(p_doc, p_node, "capture_speed", reinterpret_cast<uint32_t&>(m_captureSpeed));
	Serializer::DeserializeVec3(p_doc, p_node, "capture_position", m_capturePosition);
	Serializer::DeserializeFloat(p_doc, p_node, "brightness", m_brightness);
	Serializer::DeserializeUint32(p_doc, p_node, "resolution", m_resolution);
	Serializer::DeserializeUint32(p_doc, p_node, "influence_policy", reinterpret_cast<uint32_t&>(m_influencePolicy));
	Serializer::DeserializeVec3(p_doc, p_node, "influence_size", m_influenceSize);
	Serializer::DeserializeBoolean(p_doc, p_node, "box_projection", m_boxProjection);

	m_captureFaceIndex = 0;
}



void Core::ECS::Components::CReflectionProbe::_NotifyCubemapComplete()
{
	if (m_captureRequest)
	{
		m_captureRequest.reset(); // Discard the capture request after processing it.
	}

	// This is to keep track of whether any cubemap is complete.
	// Useful to ensure that we always have a valid cubemap to present.
	m_isAnyCubemapComplete = true;

	// Once the cubemap is complete, we can generate the mipmaps
	// that will be used to sample the average color of the cubemap (diffuse IBL).
	m_cubemapIterator[kBackBufferIndex]->GenerateMipmaps();

	// When using double buffering (i.e. progressive capture), we need to
	// update which buffer is the back buffer and which one is the complete buffer (presented to shaders).
	if (_IsDoubleBuffered())
	{
		++m_cubemapIterator;
		++m_framebuffers;
	}
}

void Core::ECS::Components::CReflectionProbe::_AllocateResources()
{
	const uint8_t cubemapCount = _IsDoubleBuffered() ? 2 : 1;

	// Reset the ping pong iterators
	m_framebuffers.Reset();
	m_cubemapIterator.Reset();

	// This forces a capture on the next frame,
	// as we always want to have at least one valid cubemap.
	m_isAnyCubemapComplete = false;

	m_captureFaceIndex = 0;

	// Allocate 1 or 2 cubemaps (depending on double buffering)
	for (uint8_t i = 0; i < cubemapCount; ++i)
	{
		auto& cubemap = m_cubemaps[i];
		cubemap = std::make_shared<::Rendering::HAL::Texture>(
			::Rendering::Settings::ETextureType::TEXTURE_CUBE,
			"ReflectionProbeCubemap"
		);

		cubemap->Allocate(
			::Rendering::Settings::TextureDesc{
				.width = m_resolution,
				.height = m_resolution,
				.minFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR_MIPMAP_LINEAR,
				.magFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR,
				.horizontalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
				.verticalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
				.internalFormat = ::Rendering::Settings::EInternalFormat::RGBA32F,
				.useMipMaps = true
			}
		);

		for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex)
		{
			m_framebuffers[i].Attach<::Rendering::HAL::Texture>(
				cubemap,
				::Rendering::Settings::EFramebufferAttachment::COLOR,
				faceIndex, // Each color attachment is a face of the cubemap
				faceIndex // Each face of the cubemap is accessed by its layer index
			);
		}

		// Depth buffer
		const auto renderbuffer = std::make_shared<::Rendering::Data::Renderbuffer>(false);
		const auto internalFormat = ::Rendering::Settings::EInternalFormat::DEPTH_COMPONENT;
		renderbuffer->Allocate(m_resolution, m_resolution, internalFormat);
		m_framebuffers[i].Attach(renderbuffer, ::Rendering::Settings::EFramebufferAttachment::DEPTH);

		// Validation
		m_framebuffers[i].Validate();
	}
}

void Core::ECS::Components::CReflectionProbe::_PrepareUBO()
{
	const auto& probePosition = owner.transform.GetWorldPosition() + m_capturePosition;
	const auto& boxPosition = owner.transform.GetWorldPosition();
	const auto& probeRotation = owner.transform.GetWorldRotation();
	const auto& probeRotationMatrix = Maths::FQuaternion::ToMatrix3(Maths::FQuaternion::Normalize(probeRotation));

	// UBO is padding sensitive, so we need to ensure proper alignment.
#pragma pack(push, 1)
	struct
	{
		Maths::FVector4 position;
		Maths::FMatrix4 rotation;
		Maths::FVector4 boxCenter;
		Maths::FVector4 boxHalfExtents;
		float brightness;
		bool boxProjection;
		std::byte padding1[3];
		bool local;
		std::byte padding2[3];
	} uboDataPage{
		.position = probePosition,
		.rotation = probeRotationMatrix,
		.boxCenter = boxPosition,
		.boxHalfExtents = m_influenceSize,
		.brightness = m_brightness,
		.boxProjection = m_boxProjection && m_influencePolicy == EInfluencePolicy::LOCAL,
		.local = m_influencePolicy == EInfluencePolicy::LOCAL,
	};
#pragma pack(pop)

	static_assert(sizeof(uboDataPage) == kUBOSize, "UBO data size mismatch");

	m_uniformBuffer->Upload(&uboDataPage);
}

std::vector<uint32_t> Core::ECS::Components::CReflectionProbe::_GetCaptureFaceIndices()
{
	auto targetFaceCount = [&]() -> uint32_t {
		const bool captureRequested = m_captureRequest.has_value();
		const bool immediateCaptureRequested =
			(captureRequested && m_captureRequest->forceImmediate) ||
			// Always make sure to capture all faces immediately when no cubemap is complete,
			// unless we are in "on demand" mode.
			(m_refreshMode != ERefreshMode::ON_DEMAND && !m_isAnyCubemapComplete);

		if (immediateCaptureRequested)
		{
			return 6; // Capture all faces immediately
		}

		if (captureRequested || m_refreshMode == ERefreshMode::REALTIME)
		{
			return static_cast<uint32_t>(m_captureSpeed);
		}

		return 0;
		}();

		if (targetFaceCount == 0)
		{
			return {}; // No faces to capture
		}

		const auto lastIndex = m_captureFaceIndex + (targetFaceCount - 1);

		assert(
			lastIndex < 6&&
			"We shouldn't request more than one cubemap to be modified at a time."
			// Expected indices are:
			// [0, 1, 2, 3, 4, 5] for 6 faces per frame,
			// [0, 1, 2], [3, 4, 5] for 3 faces per frame,
			// [0, 1], [2, 3], [4, 5] for 2 faces per frame,
			// [0], [1], [2], [3], [4], [5] for 1 face per frame.
		);

		std::vector<uint32_t> faceIndices;
		faceIndices.reserve(targetFaceCount);

		for (uint32_t i = m_captureFaceIndex; i <= lastIndex; ++i)
		{
			faceIndices.push_back(i);
		}

		// Update the index for the next capture
		m_captureFaceIndex = (lastIndex + 1) % 6;

		return faceIndices;
}

Rendering::Data::Framebuffer& Core::ECS::Components::CReflectionProbe::_GetTargetFramebuffer() const
{
	auto& framebuffer =
		_IsDoubleBuffered() ?
		m_framebuffers[kBackBufferIndex] :
		m_framebuffers[0];

	assert(framebuffer.IsValid()&&"Framebuffer is invalid");
	return framebuffer;
}

Rendering::Data::BufferObject& Core::ECS::Components::CReflectionProbe::_GetUniformBuffer() const
{
	assert(m_uniformBuffer != nullptr&&"Uniform buffer is not initialized");
	return *m_uniformBuffer;
}

bool Core::ECS::Components::CReflectionProbe::_IsDoubleBuffered() const
{
	return RequiresDoubleBuffering(m_captureSpeed);
}
