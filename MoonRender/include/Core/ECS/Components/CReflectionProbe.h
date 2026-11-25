#pragma once
#include <Core/ECS/Components/AComponent.h>
#include <Core/Rendering/PingPongFramebuffer.h>
#include <Rendering/Data/Framebuffer.h>
#include <Rendering/Data/Texture.h>
#include <Rendering/Data/BufferObject.h>

namespace Core::ECS { class Actor; }

namespace Core::Rendering
{
	class ReflectionRenderPass;
	class ReflectionRenderFeature;
}

namespace Core::ECS::Components
{
	/**
	* Component that captures the surrounding environment into a cubemap texture.
	*/
	class CReflectionProbe : public AComponent
	{
	public:
		enum class ERefreshMode : uint32_t
		{
			REALTIME,
			ONCE,
			ON_DEMAND
		};

		enum class ECaptureSpeed : uint32_t
		{
			ONE_FACE = 1,		// Capture one face per frame
			TWO_FACES = 2,		// Capture two faces per frame
			THREE_FACES = 3,	// Capture three faces per frame
			SIX_FACES = 6		// Capture all six faces per frame
		};

		enum class EInfluencePolicy : uint32_t
		{
			LOCAL,
			GLOBAL
		};

		/**
		* Constructor
		* @param p_owner
		*/
		CReflectionProbe(ECS::Actor& p_owner);

		/**
		* Returns the name of the component
		*/
		std::string GetName() override;

		/**
		* Sets the refresh mode of the reflection probe
		* @param p_mode
		*/
		void SetRefreshMode(ERefreshMode p_mode);

		/**
		* Returns the refresh mode of the reflection probe
		*/
		ERefreshMode GetRefreshMode() const;

		/**
		* Determines how many faces the reflection probe should capture per frame
		* @param p_speed
		*/
		void SetCaptureSpeed(ECaptureSpeed p_speed);

		/**
		* Returns the capture speed (number of faces captured per frame)
		*/
		ECaptureSpeed GetCaptureSpeed() const;

		/**
		* Sets the position offset for the capture of the reflection probe
		* @param p_position
		*/
		void SetCapturePosition(const Maths::FVector3& p_position);

		/**
		* Returns the position offset for the capture of the reflection probe
		*/
		const Maths::FVector3& GetCapturePosition() const;

		/**
		* Sets the brightness of the reflection probe
		* @param p_brightness
		*/
		void SetBrightness(float p_brightness);

		/**
		* Returns the brightness of the reflection probe
		*/
		float GetBrightness() const;

		/**
		* Sets the cubemap resolution
		* @note The resolution must be a power of 2!
		* @param p_resolution
		*/
		void SetCubemapResolution(uint32_t p_resolution);

		/**
		* Returns the cubemap resolution
		*/
		uint32_t GetCubemapResolution() const;

		/**
		* Determines the influence policy of the reflection probe
		* @param p_policy
		*/
		void SetInfluencePolicy(EInfluencePolicy p_policy);

		/**
		* Returns the influence policy of the reflection probe
		*/
		EInfluencePolicy GetInfluencePolicy() const;

		/**
		* Sets the size of the influence volume of the reflection probe
		* @param p_size
		*/
		void SetInfluenceSize(const Maths::FVector3& p_size);

		/**
		* Returns the size of the reflection probe volume
		*/
		const Maths::FVector3& GetInfluenceSize() const;

		/**
		* Sets if the reflection probe should use box projection
		*/
		void SetBoxProjection(bool p_enabled);

		/**
		* Returns if the reflection probe uses box projection
		*/
		bool IsBoxProjectionEnabled() const;

		/**
		* Requests the cubemap to be updated
		* @param p_forceImmediate
		*/
		void RequestCapture(bool p_forceImmediate = false);

		/**
		* Returns the last complete cubemap captured by the reflection probe
		*/
		std::shared_ptr<::Rendering::Data::Texture> GetCubemap() const;

		/**
		* Serialize the component
		* @param p_doc
		* @param p_node
		*/
		virtual void OnSerialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node) override;

		/**
		* Deserialize the component
		* @param p_doc
		* @param p_node
		*/
		virtual void OnDeserialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node) override;


	private:
		void _NotifyCubemapComplete();
		void _AllocateResources();
		void _PrepareUBO();
		std::vector<uint32_t> _GetCaptureFaceIndices();
		::Rendering::Data::Framebuffer& _GetTargetFramebuffer() const;
		::Rendering::Data::BufferObject& _GetUniformBuffer() const;
		bool _IsDoubleBuffered() const;

		friend class Core::Rendering::ReflectionRenderPass;
		friend class Core::Rendering::ReflectionRenderFeature;

	private:
		struct CaptureRequestDesc
		{
			bool forceImmediate = false;
		};

	private:
		// Double buffering so we can render to progressively one while reading from the other
		Rendering::PingPongFramebuffer m_framebuffers;
		std::array<std::shared_ptr<::Rendering::Data::Texture>, 2> m_cubemaps;
		Tools::Utils::CircularIterator<std::shared_ptr<::Rendering::Data::Texture>, 2> m_cubemapIterator;
		std::unique_ptr<::Rendering::Data::BufferObject> m_uniformBuffer;
		uint32_t m_captureFaceIndex = 0; // Current frame index in the capture process
		std::optional<CaptureRequestDesc> m_captureRequest = std::nullopt;
		bool m_isAnyCubemapComplete = false;

		// Serialized properties
		ERefreshMode m_refreshMode = ERefreshMode::REALTIME;
		ECaptureSpeed m_captureSpeed = ECaptureSpeed::ONE_FACE; // Number of faces to capture per frame
		uint32_t m_resolution = 512;
		Maths::FVector3 m_capturePosition{ 0.0f, 0.0f, 0.0f };
		float m_brightness = 1.0f;
		EInfluencePolicy m_influencePolicy = EInfluencePolicy::GLOBAL;
		Maths::FVector3 m_influenceSize{ 10.0f, 10.0f, 10.0f };
		bool m_boxProjection = false;
	};
}
